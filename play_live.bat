@echo off
chcp 65001 >nul
REM ========================================
REM  GDLearnCPP Bot - Live Play Deployer
REM  Attaches to real Geometry Dash and plays
REM ========================================

title GD Bot - Live Play

set EXE_PATH=G:\gd-ml-bot\build\Release\GDLearnCPP.exe
set CHECKPOINT_DIR=G:\gd-ml-bot\checkpoints

echo ========================================
echo   Geometry Dash ML Bot - Live Play
echo ========================================
echo.

REM Check if executable exists
if not exist "%EXE_PATH%" (
    echo [ERREUR] Executable introuvable: %EXE_PATH%
    echo Veuillez compiler le projet d'abord avec CMake.
    pause
    exit /b 1
)

REM Find best available checkpoint
echo [INFO] Recherche du meilleur checkpoint...
set CHECKPOINT=%CHECKPOINT_DIR%\latest

if exist "%CHECKPOINT%" (
    echo [OK] Checkpoint trouve: latest
    goto :play
)

REM Try other checkpoints
if exist "%CHECKPOINT_DIR%\final" (
    set CHECKPOINT=%CHECKPOINT_DIR%\final
    echo [OK] Checkpoint trouve: final
    goto :play
)

if exist "%CHECKPOINT_DIR%\best" (
    set CHECKPOINT=%CHECKPOINT_DIR%\best
    echo [OK] Checkpoint trouve: best
    goto :play
)

echo.
echo [AVERTISSEMENT] Aucun checkpoint trouve dans %CHECKPOINT_DIR%
echo Les checkpoints disponibles sont:
dir /b "%CHECKPOINT_DIR%\*.bin" 2>nul || dir /b "%CHECKPOINT_DIR%\*" 2>nul
echo.
echo Entrez le nom du checkpoint a utiliser (ou 'latest' si vous voulez en creer un nouveau):
set /p CHECKPOINT_NAME=
if "%CHECKPOINT_NAME%"=="" (
    echo [ERREUR] Aucun checkpoint specifie.
    pause
    exit /b 1
)
set CHECKPOINT=%CHECKPOINT_DIR%\%CHECKPOINT_NAME%

:play
echo.
echo [DEPLOIEMENT] Lancement du bot avec: %CHECKPOINT%
echo.
echo ========================================
echo  IMPORTANT: Assurez-vous que
echo  Geometry Dash est EN COURS D'EXECUTION!
echo ========================================
echo.
echo Le bot va:
echo  1. Attacher a la memoire de GD
echo  2. Lire la position du joueur
echo  3. Prendre le controle des entrees (souris/clavier)
echo  4. Jouer automatiquement!
echo.
echo Appuyez sur une touche pour demarrer...
pause >nul

echo.
echo [LANCEMENT] Demarrage du bot...
"%EXE_PATH%" play "%CHECKPOINT%"

echo.
echo ========================================
echo  Bot arrete.
echo ========================================
pause
