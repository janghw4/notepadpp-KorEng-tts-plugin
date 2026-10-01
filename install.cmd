@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0analysis\install.ps1"
if errorlevel 1 (
  echo.
  echo If access was denied, right-click install.cmd and select Run as administrator.
)
pause
