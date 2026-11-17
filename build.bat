@echo off
setlocal
cd /d "%~dp0src"
set "VCVARS="
for %%P in (
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) do if exist %%P if not defined VCVARS set "VCVARS=%%~P"

if not defined VCVARS (
  echo ERROR: vcvars64.bat not found. Install Visual Studio 2022 Build Tools / C++ desktop workload.
  exit /b 1
)

call "%VCVARS%"
if errorlevel 1 exit /b 1

msbuild BonDriver_BDA.sln /t:BonDriver_BDA /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m
exit /b %ERRORLEVEL%
