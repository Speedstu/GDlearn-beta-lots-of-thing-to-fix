@echo off
echo ==========================================
echo   GD-ML-Bot External Visualizer
echo   Completely decoupled from training!
echo   Training speed is NOT affected.
echo ==========================================
echo.
echo Starting HTTP server...
start "GD-Visualizer Server" python "%~dp0viz_server.py"
timeout /t 1 /nobreak >nul
echo Opening visualizer in browser...
start "" "http://localhost:8888/visualizer.html"
echo.
echo [OK] Visualizer open! Training runs at full speed.
echo [OK] Close this window or press Ctrl+C to stop the server.
echo.
pause
