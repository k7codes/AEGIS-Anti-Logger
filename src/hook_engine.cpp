#include "hook_engine.hpp"
#include "common.hpp"

namespace aegis {

namespace {
constexpr UINT WM_APP_REFRESH = WM_APP + 0xA1; // refresh thread -> pump: hook'u yeniden kur
constexpr UINT WM_APP_STOP    = WM_APP + 0xA2; // pump thread'i sonlandir

// Dokunma ALANI (kasitli kucuk): SADECE harfler korunuyor.
// Kullanici talebi: Ctrl ve benzeri tuslar dahil hicbir seye dokunulmasin,
// sadece harfler engellenip yeniden basilsin.
bool IsProtectedLetter(DWORD vk) {
    return vk >= 0x41 && vk <= 0x5A; // VK_A..VK_Z
}

// ComboContext: kullanici su an Ctrl/Alt/Win basili tutuyor mu?
// Hook'ta GetAsyncKeyState okunmasi sentronik: halihazirda tutulan
// modifier'in durumunu yansitir (o an yenisi kuyruga girmis bile olsa
// sonraki event'te yakalanir, guvenli.
bool IsComboContext() {
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    return down(VK_LCONTROL) || down(VK_RCONTROL) ||
           down(VK_LMENU)   || down(VK_RMENU)   ||
           down(VK_LWIN)    || down(VK_RWIN);
}
}

HookEngine& HookEngine::instance() {
    static HookEngine s;
    return s;
}

// ---- tek tus gonderme yardimcisi (gurultu motoru da kullanir) ----
bool SendKeyPair(WORD vk, WORD scan, bool extended, DWORD extraInfo) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk  = vk;
    in[0].ki.wScan = scan;
    in[0].ki.dwExtraInfo = extraInfo;
    in[1].ki.wVk  = vk;
    in[1].ki.wScan = scan;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[1].ki.dwExtraInfo = extraInfo;
    if (extended) {
        in[0].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        in[1].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    return SendInput(2, in, sizeof(INPUT)) == 2;
}

// ---- LL hook geri cagrisi ----
LRESULT CALLBACK HookEngine::llProc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION)
        return CallNextHookEx(nullptr, code, wp, lp);

    const KBDLLHOOKSTRUCT* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    const bool injected = (kb->flags & LLKHF_INJECTED) != 0;
    const uint64_t extra = static_cast<uint64_t>(kb->dwExtraInfo);

    // 1) KENDI enjeksiyonumuz mu?
    if (injected && IsOursExtra(extra)) {
        const InjectKind kind = KindOfExtra(extra);
        if (kind == InjectKind::Noise) {
            if (engineFlags().forwardNoise.load(std::memory_order_relaxed)) {
                // zincire ilet: bizden ONCE kurulmus hook'lar (eski logger'lar)
                // bunlari cope dolgu olarak kaydeder; uygulamalarda VK(0xFF/0x07) no-op'tur.
                return CallNextHookEx(nullptr, code, wp, lp);
            }
            stats().noiseSwallowed.fetch_add(1, std::memory_order_relaxed);
            return 1; // tamamen yut: uygulama hicbir sey gormez
        }
        // Real = bizim yolladığımız gerçek tuş -> sisteme (uygulamaya) ilet.
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    // 2) Baskasinin enjeksiyonu (uzak masaustu, makro, erisilebilirlik yazilimi...) -> dokunma
    if (injected) {
        stats().foreignInjected.fetch_add(1, std::memory_order_relaxed);
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    // 3) GERCEK kullanici tusu
    stats().realSeen.fetch_add(1, std::memory_order_relaxed);

    if (!engineFlags().enabled.load(std::memory_order_relaxed))
        return CallNextHookEx(nullptr, code, wp, lp); // koruma kapali: normal akis

    if (!engineFlags().aggressive.load(std::memory_order_relaxed))
        return CallNextHookEx(nullptr, code, wp, lp); // sadece gurultu modu: tuslara dokunma

    // modifier/kombinasyon korumasi: Ctrl+C, Win+D, F5 vs. bozulmasin.
    // Ayrica Ctrl/Alt/Win basiliyken harfe bile mudahale edilmez.
    if (!IsProtectedLetter(kb->vkCode) || IsComboContext()) {
        stats().passthrough.fetch_add(1, std::memory_order_relaxed);
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    // AGRESIF: gercek tusu geri al, kendimiz yeniden basalim.
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk  = static_cast<WORD>(kb->vkCode);
    in.ki.wScan = static_cast<WORD>(kb->scanCode);
    in.ki.dwExtraInfo = MakeExtra(InjectKind::Real);
    if (kb->flags & LLKHF_EXTENDED) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (kb->flags & LLKHF_UP)       in.ki.dwFlags |= KEYEVENTF_KEYUP;

    const UINT sent = SendInput(1, &in, sizeof(INPUT));
    if (sent == 1) {
        stats().blocked.fetch_add(1, std::memory_order_relaxed);
        return 1; // orijinal event bu zincirden oteye gecmez; yerine bizim yeniden yolladigimiz kopya gider.
    }
    // SendInput basarisiz oldu (nadir); tuS kaybolmasin -> orjinaline izin ver.
    return CallNextHookEx(nullptr, code, wp, lp);
}

// ---- hook thread: hook kurar, mesajla run'u döndürür ----
DWORD WINAPI HookEngine::pumpProc(LPVOID self) {
    HookEngine& s = *reinterpret_cast<HookEngine*>(self);

    s.hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &HookEngine::llProc, GetModuleHandleW(nullptr), 0);
    s.hookInstalled_.store(s.hook_ != nullptr, std::memory_order_release);
    s.pumpThreadId_ = GetCurrentThreadId();

    MSG msg;
    while (s.running_.load(std::memory_order_acquire)) {
        const int r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) break;
        if (msg.message == WM_APP_STOP) break;
        if (msg.message == WM_APP_REFRESH) s.reinstall();
    }

    if (s.hook_) {
        UnhookWindowsHookEx(s.hook_);
        s.hook_ = nullptr;
    }
    s.hookInstalled_.store(false, std::memory_order_release);
    return 0;
}

// zincirimizi gecirtebilmek icin her rehookSec saniyede bir yeniden kur.
DWORD WINAPI HookEngine::refreshProc(LPVOID self) {
    HookEngine& s = *reinterpret_cast<HookEngine*>(self);
    Sleep(300); // pump thread'inin id döngüsüne/hook'a girmesini bekle
    while (s.running_.load(std::memory_order_acquire)) {
        int sec = engineFlags().rehookSec.load(std::memory_order_relaxed);
        if (sec < 1)  sec = 1;
        if (sec > 60) sec = 60;
        // 50ms dilimler halinde bekle -> hızlı stop
        for (int i = 0; i < sec * 20 && s.running_.load(std::memory_order_acquire); ++i)
            Sleep(50);
        if (!s.running_.load(std::memory_order_acquire)) break;
        if (s.pumpThreadId_)
            PostThreadMessageW(s.pumpThreadId_, WM_APP_REFRESH, 0, 0);
    }
    return 0;
}

void HookEngine::reinstall() {
    if (hook_) UnhookWindowsHookEx(hook_);
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &HookEngine::llProc, GetModuleHandleW(nullptr), 0);
    hookInstalled_.store(hook_ != nullptr, std::memory_order_release);
}

bool HookEngine::start() {
    if (running_.load(std::memory_order_acquire)) return true;
    running_.store(true, std::memory_order_release);
    stats().startTime.store(GetTickCount64(), std::memory_order_relaxed);

    pumpThread_ = CreateThread(nullptr, 0, &HookEngine::pumpProc, this, 0, nullptr);
    refreshThread_ = CreateThread(nullptr, 0, &HookEngine::refreshProc, this, 0, nullptr);
    // hook kurulumunu bekle (kisa bir mola)
    for (int i = 0; i < 100 && !hookInstalled_.load(std::memory_order_acquire); ++i)
        Sleep(10);
    return hookInstalled_.load(std::memory_order_acquire);
}

void HookEngine::stop() {
    if (!running_.exchange(false)) return;
    if (pumpThreadId_)
        PostThreadMessageW(pumpThreadId_, WM_APP_STOP, 0, 0);
    if (pumpThread_)    { WaitForSingleObject(pumpThread_, 3000);    CloseHandle(pumpThread_);    pumpThread_ = nullptr; }
    if (refreshThread_) { WaitForSingleObject(refreshThread_, 3000); CloseHandle(refreshThread_); refreshThread_ = nullptr; }
    pumpThreadId_ = 0;
}

} // namespace aegis
