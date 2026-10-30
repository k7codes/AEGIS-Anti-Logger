#include "noise_engine.hpp"
#include "common.hpp"
#include "hook_engine.hpp"
#include <random>

namespace aegis {

NoiseEngine& NoiseEngine::instance() {
    static NoiseEngine s;
    return s;
}

DWORD WINAPI NoiseEngine::loopProc(LPVOID self) {
    NoiseEngine& s = *reinterpret_cast<NoiseEngine*>(self);

    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> scanDist(0x00, 0xFF);
    std::uniform_int_distribution<int> vkRoll(0, 99);

    while (s.running_.load(std::memory_order_acquire)) {
        // ters exponansiyel dagilim -> keys/sec ortalamayi korur ama ritim insanimsi bozuklukta
        const double rate = engineFlags().noisePerSecond.load(std::memory_order_relaxed);
        const double clampedRate = rate < 1.0 ? 1.0 : (rate > 500.0 ? 500.0 : rate);
        double mean = 1000.0 / clampedRate;
        double interval = mean * (0.35 + 1.30 * unit(rng));
        if (interval < 2.0)   interval = 2.0;
        if (interval > 300.0) interval = 300.0;

        // bekleme: 40ms dilimler (hizli stop icin)
        int left = static_cast<int>(interval);
        while (left > 0 && s.running_.load(std::memory_order_acquire)) {
            int step = left < 40 ? left : 40;
            Sleep(static_cast<DWORD>(step));
            left -= step;
        }
        if (!s.running_.load(std::memory_order_acquire)) break;

        // koruma kapaliyken gurultu yok
        if (!engineFlags().enabled.load(std::memory_order_relaxed))
            continue;

        // rastgele no-op tus: %85 VK 0xFF, %15 VK 0x07 (ikisi de "unassigned")
        WORD vk = (vkRoll(rng) < 85) ? 0xFF : 0x07;
        WORD scan = static_cast<WORD>(scanDist(rng));
        const bool extended = (scanDist(rng) % 5) == 0; // genisletilmis tus varyasyonu
        if (SendKeyPair(vk, scan, extended, MakeExtra(InjectKind::Noise)))
            stats().noiseSent.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

bool NoiseEngine::start() {
    if (running_.exchange(true)) return true;
    HANDLE h = CreateThread(nullptr, 0, &loopProc, this, 0, nullptr);
    if (!h) { running_.store(false); return false; }
    CloseHandle(h); // handle gerekmiyor, thread devam eder
    return true;
}

void NoiseEngine::stop() {
    running_.store(false, std::memory_order_release);
}

} // namespace aegis
