@echo off
setlocal enabledelayedexpansion

cd /d "d:\ycIDE-electron\lib\krnln\vs2022"

set "MSBUILD=D:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

echo Building krnln library...
"%MSBUILD%" krnln.vcxproj -p:Configuration=Release -p:Platform=x64

echo.
echo Build completed.
pause
