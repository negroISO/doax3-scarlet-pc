@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  python -B tools\Run-Game.py --neural 0 %*
) else (
  py -3 -B tools\Run-Game.py --neural 0 %*
)
exit /b %errorlevel%
