@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS exit /b 1
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
rc /nologo /DUNICODE /D_UNICODE /fo build\LiveWallpaper.res src\LiveWallpaper.rc || exit /b 1
cl /nologo /std:c++20 /utf-8 /O2 /GL /W4 /WX /EHsc /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MT /I src ^
  src\host_main.cpp src\core.cpp src\wallpaper_host.cpp src\mp4_player.cpp build\LiveWallpaper.res ^
  /Fe:build\LiveWallpaper.Host.exe ^
  /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\LiveWallpaper.manifest /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT ^
  user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib dwmapi.lib mf.lib mfplat.lib mfuuid.lib mfplay.lib wtsapi32.lib advapi32.lib || exit /b 1
build\LiveWallpaper.Host.exe --smoke
exit /b %errorlevel%
