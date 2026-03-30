@echo off
echo ============================================
echo   GDLearnCPP - Training with Visualizer
echo   Real-time level rendering
echo   Press ESC to close, Ctrl+C to stop
echo ============================================
echo.

set EXE=G:\gd-ml-bot\build\Release\GDLearnCPP.exe

%EXE% train --mode ppo --levels tutorial --infinite --visual

pause
