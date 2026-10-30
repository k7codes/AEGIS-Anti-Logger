#pragma once
#include <windows.h>
#include <atomic>

namespace aegis {

// Random noise motoru: surekli cop tusa basar.
//  - Tuslar LLKHF_INJECTED + bizim imza ile isaretlidir.
//  - VK olarak 0xFF / 0x07 (tanimsiz/reserved) kullanilir: uygulamalarda
//    karakter uretmez, kisayol tetiklemez, notepad'e bir sey dusmez.
//  - Log tutan her ortam (hook tabanli ya da GetAsyncKeyState tabanli)
//    bu gurultuyu gercek aktivity ile karisir.
class NoiseEngine {
public:
    static NoiseEngine& instance();
    bool start();
    void stop();

private:
    NoiseEngine() = default;
    ~NoiseEngine() { stop(); }
    static DWORD WINAPI loopProc(LPVOID self);

    std::atomic<bool> running_{false};
};

} // namespace aegis
