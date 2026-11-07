@echo off
rem Double-click to remove the Pentadeck stats agent (see uninstall-windows.ps1).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-windows.ps1" %*
echo.
pause
