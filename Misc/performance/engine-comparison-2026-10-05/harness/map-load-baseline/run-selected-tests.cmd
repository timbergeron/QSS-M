@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set CC=cl
"C:\Users\Tim Bergeron\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 Misc\stress\test_image_file_candidates.py
if errorlevel 1 exit /b 1
"C:\Users\Tim Bergeron\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 Misc\stress\test_observer_hud_cache.py
if errorlevel 1 exit /b 1
"C:\Users\Tim Bergeron\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 Misc\stress\test_startup_shaders.py
if errorlevel 1 exit /b 1
"C:\Users\Tim Bergeron\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -X utf8 Misc\stress\test_external_brush_cache.py
