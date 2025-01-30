@echo off
setlocal enabledelayedexpansion

if not exist ..\build mkdir ..\build
pushd ..\build
cl /Zi /Od /W4 /DENGINE_DEBUG /nologo /wd4201 ..\code\main.c /link /incremental:no /out:engine.exe user32.lib gdi32.lib d3d11.lib dxguid.lib winmm.lib d3dcompiler.lib
popd
if errorlevel 1 exit

endlocal
