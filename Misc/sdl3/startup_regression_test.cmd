@echo off
setlocal
cd /d "%~dp0..\.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "qssm_test_vs=%%i"
if not defined qssm_test_vs exit /b 1
call "%qssm_test_vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined QSSM_TEST_PYTHON set "QSSM_TEST_PYTHON=python"
set "CC=cl"
"%QSSM_TEST_PYTHON%" Misc\stress\test_startup_shaders.py
if errorlevel 1 exit /b 1
"%QSSM_TEST_PYTHON%" Misc\stress\test_startup_controllers.py
exit /b %errorlevel%
