# AEGIS Anti Logger

Kullanıcı modunda çalışan bir **keylogger baskılayıcı**. Amaç: sen klavyeyle
yazarken arka planda sürekli **rastgele çöp tuş** üreterek (gürültü motoru)
tutulan logları işe yaramaz hale getirmek ve agresif modda gerçek tuşlarını
keylogger'lara göstermeden yakalayıp kendisi yeniden basmek.

## Mantık

1. **WH_KEYBOARD_LL hook** — AEGIS açılışta hook'unu kurar ve her
   `rehookSec` saniyede bir hook'u yeniden kurar (Windows hook zinciri
   "en son kurulan ilk çalışır" mantığındadır; biz hep zincirin başında
   kalmaya çalışırız, böylece bizden sonra kurulan bir keylogger'ın
   gerçek tuşları görmesini kaçınılmaz olarak kısa pencerelere sıkıştırır).
2. **Agresif mod** — gerçek tuş **bloke** edilir (zincire iletilmez), ardından
   SendInput ile AEGIS imzası (`dwExtraInfo`) taşınan bir kopya basılır.
   Gerçek event keylogger'lara asla ulaşmaz; onlar yalnızca bizim kopyamızı
   görür. Yutulan her çöp tuş **VK 0xFF / 0x07**'dir (tanımsız = uygulamalarda
   hiçbir karakter üretmez, ne notepad'e ne şifre alanına bir şey düşmez).
3. **Gürültü modu** — gerçek tuşlara hiç dokunmaz, sadece log çöplüğü üretir.
4. **Başlangıç** — `HKCU\...\Run` kaydı (yönetici istermez). `--silent`
   parametresiyle sessiz başlar; `aegis.exe --uninstall` kaydı temizler.
5. **Arayüz** — Dear ImGui (D3D11): mod seçimi, gürültü yoğunluğu,
   başlangıç yönetimi ve canlı istatistikler. `aegis.ini` ile kalıcı.

## Derleme

```powershell
# MinGW-w64 (WinLibs) + CMake kurulu olmalı
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# çıktı: build\bin\aegis.exe
```

## Kullanım

- `aegis.exe` — arayüzü açar
- `aegis.exe --silent` — gizli başlar (Run kaydı bunda kullanılır)
- `aegis.exe --uninstall` — başlangıç kaydını kaldırır
- `aegis.exe` zaten çalışırken tekrar çalıştırırsan mevcut instance'ın
  penceresini öne getirir.

### aegis.ini (exe'nin yanında)

| anahtar | anlamı |
|---|---|
| `enabled` | koruma ana anahtarı |
| `aggressive` | 1 = bloke+yeniden bas, 0 = sadece gürültü |
| `forwardNoise` | gürültüyü işimizden önce kurulmuş hook'lara ilet |
| `autostart` | Run kaydı |
| `startHidden` | `--silent` ile açılışta pencereyi gizle |
| `noisePerSecond` | saniyedeki çöp tuş sayısı (1–500) |
| `rehookSec` | hook yenileme periyodu (1–60) |

## Dürüst sınırlar

- **Hook/API tabanlı (SetWindowsHookEx, LL hook) keylogger'lara karşı** en
  güçlü usermode cevap budur; çoğu tüketici keylogger'ı bu sınıftandır.
- Bizden **sonra** kurulan hook'lara karşı `rehookSec` penceresi kadar
  (varsayılan ~3 sn'lik bir an) gerçek tuşun sızma riski vardır.
- **GetAsyncKeyState / polling tabanlı logger'lar** klavyenin global durumunu
  okur; hook bunları engelleyemez, ancak gürültü yoğunluğu olsa olsa yanıt
  ayıklamayı pahalılaştırır (tam çözümler sürücü (kernel) katmanıdır).
- Kernel sürücüsü düzeyinde kurulmuş (rootkit tarzı) keylogger'lar
  usermode'dan durdurulamaz.

## Dosyalar

```
src/common.hpp        imza sistemi (dwExtraInfo), config & istatistik çekirdeği
src/hook_engine.*     LL hook, bloke+yeniden-bas, periyodik rehook
src/noise_engine.*    gürültü motoru (ters-exponansiyel ritim)
src/config_store.*    aegis.ini okuma/yazma
src/persistence.*     HKCU Run kaydı
src/app.cpp           Win32 + D3D11 + ImGui arayüz
third_party/imgui     Dear ImGui 1.93 (ocornut)
```
