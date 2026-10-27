@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  python -B tools\Run-Game.py --prefer-neural %*
) else (
  py -3 -B tools\Run-Game.py --prefer-neural %*
)
exit /b %errorlevel%
