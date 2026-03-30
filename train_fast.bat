@echo off
echo ============================================
echo   GDLearnCPP - Fast Infinite Training
echo   All 22 levels (tutorial + 21 official)
echo   No visualizer - maximum speed
echo   Auto-save every 60s, Ctrl+C to stop
echo ============================================
echo.

set EXE=G:\gd-ml-bot\build\Release\GDLearnCPP.exe

:: Fresh start: delete old checkpoints
:: Uncomment next line to start fresh:
:: del /q G:\gd-ml-bot\checkpoints\* 2>nul

%EXE% train --mode ppo --levels tutorial --infinite

pause
