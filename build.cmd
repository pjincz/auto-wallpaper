@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist dist mkdir dist
rc /nologo /fo dist\app.res /i src src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN src\main.cpp dist\app.res /Fo:dist\main.obj /Fe:dist\AutoWallpaper.exe /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib ole32.lib winhttp.lib windowsapp.lib windowscodecs.lib
exit /b %errorlevel%
