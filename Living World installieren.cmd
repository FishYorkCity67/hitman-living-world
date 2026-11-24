@echo off
chcp 65001 >nul
echo Living World - KI-NPCs ^& Realismus fuer HITMAN
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1"
echo.
pause
