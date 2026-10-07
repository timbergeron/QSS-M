@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set CC=cl
for %%t in (%*) do (
  echo === %%t
  python -X utf8 Misc\stress\%%t
)
