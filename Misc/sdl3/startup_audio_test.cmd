@echo off
setlocal
cd /d "%~dp0..\.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "qssm_test_vs=%%i"
if not defined qssm_test_vs exit /b 1
call "%qssm_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist .codex-build\startup-audio mkdir .codex-build\startup-audio
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /IQuake /IWindows\SDL3\include Misc\sdl3\startup_audio_test.c /Fe:.codex-build\startup-audio\startup_audio_test.exe /Fo:.codex-build\startup-audio\startup_audio_test.obj /link /LIBPATH:Windows\SDL3\lib\x64 SDL3.lib
if errorlevel 1 exit /b 1
copy /y Windows\SDL3\lib\x64\SDL3.dll .codex-build\startup-audio\ >nul
.codex-build\startup-audio\startup_audio_test.exe
exit /b %errorlevel%
