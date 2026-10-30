#pragma once
// AEGIS Anti Logger - ortak tanimlar
// imza sistemi: kendi enjekte ettigimiz event'leri dwExtraInfo ile isaretleriz.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace aegis {

// dwExtraInfo damgasi. Alt 2 bit InjectKind icin rezerve edilmistir,
// bu yuzden sigortanin alt 2 biti sifir olmali.
constexpr uint32_t kSignature = 0xA61C0F10u; // ...0001 0000 -> alt 2 bit 00

enum class InjectKind : uint32_t {
    Real  = 1, // gercek kullanici tusunun yeniden enjekte edilmis hali
    Noise = 2, // gurultu motoru cop tusu
};

constexpr inline uint32_t    MakeExtra(InjectKind k) { return kSignature | static_cast<uint32_t>(k); }
constexpr inline bool        IsOursExtra(uint64_t e) { return (static_cast<uint32_t>(e) & ~0x3u) == kSignature && (e & 0x3u) != 0; }
constexpr inline InjectKind  KindOfExtra(uint64_t e) { return static_cast<InjectKind>(e & 0x3u); }

struct Config {
    bool  enabled      = true;  // genel koruma anahtari
    bool  aggressive   = true;  // true: engelle + yeniden bas; false: sadece gurultu
    bool  forwardNoise = true;  // gurultuyu hook zincirine ilet (bizden ONCE kurulmus logger'lara karsi)
    bool  autostart    = true;  // HKCU\...\Run kaydi
    bool  startHidden  = false; // Windows ile acilista pencereyi gosterme
    float noisePerSecond = 24.0f;
    int   rehookSec    = 3;     // LL hook'u bu saniyede bir yeniden kur -> zincir basina don

    // --- gorunum (Cobalt-stili tema) ---
    int   accentIdx  = 0;     // vurgu rengi preset indeksi
    int   bgFx       = 0;     // 0: aurora, 1: dalga cizgileri, 2: duz
    bool  dotsFx     = true;  // fareyi takip eden nokta izgarasi
    float glass      = 0.85f; // panel cami opakligi 0..1
    bool  toasts     = true;  // kose bildirimleri

    // --- kilit (sifre) ---
    std::string passHash;     // bos = sifre yok -> ilk acilista kurulum ekrani

    // --- kisi adi ---
    std::string userName;     // bos ise otomatik Windows kullanici adi kullanilir
};

// Atomic yansima (hook thread'i her event'te mutex'e girmesin diye):
struct EngineFlags {
    std::atomic<bool>    enabled{true};
    std::atomic<bool>    aggressive{true};
    std::atomic<bool>    forwardNoise{true};
    std::atomic<float>   noisePerSecond{24.0f};
    std::atomic<int>     rehookSec{3};
};

struct Stats {
    std::atomic<uint64_t> realSeen{0};        // gercek kullanici tuslari
    std::atomic<uint64_t> blocked{0};         // keylogger'a gostermeyip sakladigimiz
    std::atomic<uint64_t> reinjected{0};      // yolladığımız gerçek kopyalar
    std::atomic<uint64_t> noiseSent{0};       // gurultu tusu (cift sayan: down+up)
    std::atomic<uint64_t> noiseSwallowed{0};  // yuttugumuz gurultu (ileriye kapali modda)
    std::atomic<uint64_t> foreignInjected{0}; // baska yazilimlarin enjeksiyonlari (dokunmadik)
    std::atomic<uint64_t> passthrough{0};     // modifier/kombinasyon tuslari - dokunmadan gecen
    std::atomic<ULONGLONG> startTime{0};
};

inline Stats&       stats()        { static Stats s; return s; }
inline EngineFlags& engineFlags()  { static EngineFlags f; return f; }
inline Config&      config()       { static Config c; return c; }
inline std::mutex&  configMutex()  { static std::mutex m; return m; }

// UI'dan degisiklik yapinca cagir; engine atomiklerini bayilik gunceller.
void publishConfig();

} // namespace aegis
