#include "config_store.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace aegis {

std::wstring ExePathW() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::wstring IniPathW() {
    std::wstring p = ExePathW();
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.resize(slash + 1);
    p += L"aegis.ini";
    return p;
}

// ---- basit INI oku / yaz --------------------------------------------
static bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (4 * 1024 * 1024)) { CloseHandle(h); return false; }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(got);
    return true;
}

// ---- hafif obfuskasyon: ini dosyasi XOR akisi + hex ile saklanir ----
static const char kScrambleSig[] = "AEGISC:";

static void ScrambleText(const std::string& plain, std::string& out) {
    unsigned long long st = 0xd1b54a32d192ed03ull;
    for (const char* q = "aegis.ini::v1"; *q; ++q) { st ^= (unsigned char)*q; st *= 1099511628211ull; }
    out = kScrambleSig;
    char hex[4];
    for (size_t i = 0; i < plain.size(); ++i) {
        st ^= (unsigned long long)i; st *= 1099511628211ull;
        unsigned char kb = (unsigned char)((st >> 37) ^ st);
        std::snprintf(hex, sizeof hex, "%02x", (unsigned)(plain[i] ^ kb));
        out += hex;
    }
}

static int HexNib(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool UnscrambleText(const std::string& raw, std::string& plain) {
    const size_t sl = sizeof kScrambleSig - 1;
    if (raw.size() < sl + 2 || raw.compare(0, sl, kScrambleSig) != 0) return false;
    unsigned long long st = 0xd1b54a32d192ed03ull;
    for (const char* q = "aegis.ini::v1"; *q; ++q) { st ^= (unsigned char)*q; st *= 1099511628211ull; }
    plain.clear();
    for (size_t i = 0; i + 1 < raw.size() - sl; i += 2) {
        int hi = HexNib(raw[sl + i]), lo = HexNib(raw[sl + i + 1]);
        if (hi < 0 || lo < 0) return false;
        st ^= (unsigned long long)(i >> 1); st *= 1099511628211ull;
        unsigned char kb = (unsigned char)((st >> 37) ^ st);
        plain += (char)((unsigned char)((hi << 4) | lo) ^ kb);
    }
    return true;
}

static void ParseIni(const std::string& text, Config& c) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        if (line[first] == '[') continue; // [section] destegi gereksiz
        size_t eq = line.find('=', first);
        if (eq == std::string::npos) continue;
        std::string key = line.substr(first, eq - first);
        size_t vstart = line.find_first_not_of(" \t", eq + 1);
        if (vstart == std::string::npos) continue;
        size_t vend = line.find_last_not_of(" \t\r");
        std::string val = line.substr(vstart, vend - vstart + 1);
        auto isTrue = [&](const char* d) { return _stricmp(val.c_str(), d) == 0; };

        if      (_stricmp(key.c_str(), "enabled")         == 0) c.enabled      = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "aggressive")     == 0) c.aggressive   = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "forwardNoise")   == 0) c.forwardNoise = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "autostart")      == 0) c.autostart    = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "startHidden")    == 0) c.startHidden  = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "noisePerSecond") == 0) c.noisePerSecond = (float)atof(val.c_str());
        else if (_stricmp(key.c_str(), "rehookSec")      == 0) c.rehookSec    = atoi(val.c_str());
        else if (_stricmp(key.c_str(), "accentIdx")      == 0) c.accentIdx    = atoi(val.c_str());
        else if (_stricmp(key.c_str(), "bgFx")           == 0) c.bgFx         = atoi(val.c_str());
        else if (_stricmp(key.c_str(), "dotsFx")         == 0) c.dotsFx       = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "glass")          == 0) c.glass        = (float)atof(val.c_str());
        else if (_stricmp(key.c_str(), "toasts")         == 0) c.toasts       = isTrue("1") || isTrue("true");
        else if (_stricmp(key.c_str(), "passHash")       == 0) c.passHash     = val;
        else if (_stricmp(key.c_str(), "userName")       == 0) c.userName     = val;
    }
    if (c.noisePerSecond < 1.0f)   c.noisePerSecond = 1.0f;
    if (c.noisePerSecond > 500.0f) c.noisePerSecond = 500.0f;
    if (c.rehookSec < 1)  c.rehookSec = 1;
    if (c.rehookSec > 60) c.rehookSec = 60;
    if (c.accentIdx < 0) c.accentIdx = 0;
    if (c.accentIdx > 7) c.accentIdx = 7;
    if (c.bgFx < 0) c.bgFx = 0;
    if (c.bgFx > 2) c.bgFx = 2;
    if (c.glass < 0.30f) c.glass = 0.30f;
    if (c.glass > 1.0f)  c.glass = 1.0f;
}

void LoadConfig() {
    Config c;
    std::string text;
    if (ReadWholeFile(IniPathW(), text)) {
        std::string plain;
        if (UnscrambleText(text, plain)) ParseIni(plain, c);
        else ParseIni(text, c); // eski duz metin formati (ilk sonraki kayitta sifirlanir)
    }
    {
        std::lock_guard<std::mutex> g(configMutex());
        config() = c;
    }
    publishConfig(); // motor atomiklerine yay
    stats().startTime.store(GetTickCount64(), std::memory_order_relaxed);
}

void SaveConfig() {
    Config c = SnapshotConfig();
    char buf[256];
    std::string out =
        "# AEGIS Anti Logger config (UTF-8)\n"
        "# enabled      : koruma ana anahtari (1/0)\n"
        "# aggressive   : 1 = tusu keylogger'a gostermeyip yeniden bas (tavsiye)\n"
        "#                0 = sadece gurultu doldur, tusa dokunma\n"
        "# forwardNoise : gurultu eski(bizden once kurulu) logger'larin zincirine ilet\n"
        "# autostart    : Windows acilisinda bizimle basla (HKCU Run)\n"
        "# startHidden  : acilisda pencereyi gizle (tekrar exe'yi calistirinca gorunur)\n"
        "# noisePerSecond: saniyedeki cop tus sayisi (1-500)\n"
        "# rehookSec    : hook'u bu saniyede bir yeniden kur (1-60)\n";
    auto addB = [&](const char* k, bool v) { buf[0] = 0; std::snprintf(buf, sizeof buf, "%s=%d\n", k, v ? 1 : 0); out += buf; };
    addB("enabled",      c.enabled);
    addB("aggressive",   c.aggressive);
    addB("forwardNoise", c.forwardNoise);
    addB("autostart",    c.autostart);
    addB("startHidden",  c.startHidden);
    std::snprintf(buf, sizeof buf, "noisePerSecond=%.2f\n", (double)c.noisePerSecond); out += buf;
    std::snprintf(buf, sizeof buf, "rehookSec=%d\n", c.rehookSec);                     out += buf;
    std::snprintf(buf, sizeof buf, "accentIdx=%d\n", c.accentIdx);                     out += buf;
    std::snprintf(buf, sizeof buf, "bgFx=%d\n", c.bgFx);                               out += buf;
    addB("dotsFx", c.dotsFx);
    std::snprintf(buf, sizeof buf, "glass=%.2f\n", (double)c.glass);                   out += buf;
    addB("toasts", c.toasts);
    out += "passHash="; out += c.passHash; out += "\n";
    out += "userName="; out += c.userName; out += "\n";

    // dosya duz metin olarak yazilmaz: XOR akisi + hex blob
    std::string blob;
    ScrambleText(out, blob);
    HANDLE h = CreateFileW(IniPathW().c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(h, blob.data(), (DWORD)blob.size(), &wrote, nullptr);
        CloseHandle(h);
    }
}

Config SnapshotConfig() {
    std::lock_guard<std::mutex> g(configMutex());
    return config();
}

void PublishConfigSnapshot(const Config& c) {
    {
        std::lock_guard<std::mutex> g(configMutex());
        config() = c;
    }
    publishConfig();
}

// motor atomiklerini configin son haliyle gunceller (UI + ini -> motorlar)
void publishConfig() {
    Config c;
    {
        std::lock_guard<std::mutex> g(configMutex());
        c = config();
    }
    auto& f = engineFlags();
    f.enabled.store(c.enabled,      std::memory_order_relaxed);
    f.aggressive.store(c.aggressive, std::memory_order_relaxed);
    f.forwardNoise.store(c.forwardNoise, std::memory_order_relaxed);
    f.noisePerSecond.store(c.noisePerSecond, std::memory_order_relaxed);
    f.rehookSec.store(c.rehookSec,  std::memory_order_relaxed);
}

} // namespace aegis
