@echo off
rem Double-click to install the Pentadeck stats agent for this Windows user (see install-windows.ps1).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-windows.ps1" %*
echo.
pause
