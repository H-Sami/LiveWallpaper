@echo off
setlocal
cd /d "%~dp0"

echo [1/2] Running core tests...
call tests\build_tests.bat || exit /b 1

echo [2/2] Building native wallpaper host...
call build_host.bat || exit /b 1

echo Built: %CD%\build\LiveWallpaper.Host.exe
exit /b 0