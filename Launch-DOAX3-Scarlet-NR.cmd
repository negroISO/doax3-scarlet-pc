@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  python -B tools\Run-Game.py --neural 1 %*
) else (
  py -3 -B tools\Run-Game.py --neural 1 %*
)
exit /b %errorlevel%
