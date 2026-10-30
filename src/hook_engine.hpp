#pragma once
#include <windows.h>
#include <thread>
#include <atomic>

namespace aegis {

// WH_KEYBOARD_LL hook'unu yönetir.
//  - Kancayı zincirin en başında tutmak için periyodik yeniden kurulur.
//  - Kişisel tuşları keylogger'lara göstermeden yakalar ve kendisi yeniden gönderir.
//  - Gurultu tuşlarını ya zincire iletir ya da yutar (config'e göre).
class HookEngine {
public:
    static HookEngine& instance();

    bool start();   // kanca is parcasi + yenileme is parcasi
    void stop();
    bool hookInstalled() const { return hookInstalled_.load(std::memory_order_acquire); }

private:
    HookEngine() = default;
    ~HookEngine() { stop(); }

    static LRESULT CALLBACK llProc(int code, WPARAM wp, LPARAM lp);
    static DWORD WINAPI    pumpProc(LPVOID self);
    static DWORD WINAPI    refreshProc(LPVOID self);
    void reinstall(); // unhook + rehook (zincirde en one gec)

    HANDLE    pumpThread_  = nullptr;
    HANDLE    refreshThread_ = nullptr;
    DWORD     pumpThreadId_  = 0;
    HHOOK     hook_          = nullptr;
    std::atomic<bool> hookInstalled_{false};
    std::atomic<bool> running_{false};
};

// klavye enjeksiyon yardimcisi (gurultu + main.cpp tekrar kullanir)
bool SendKeyPair(WORD vk, WORD scan, bool extended, DWORD extraInfo);

} // namespace aegis
