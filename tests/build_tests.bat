@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
if not exist build\tests mkdir build\tests
cl /nologo /std:c++20 /utf-8 /W4 /WX /EHsc /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /MTd /I src tests\core_tests.cpp src\core.cpp /Fe:build\CoreTests.exe || exit /b 1
build\CoreTests.exe
exit /b %errorlevel%
