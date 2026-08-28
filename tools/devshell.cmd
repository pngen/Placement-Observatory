@echo off
rem --- Placement Observatory dev shell: source VS x64 toolchain, then run %* ---
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINST="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINST=%%i"
)
if not defined VSINST (
  echo ERROR: no Visual Studio 2022 x64 toolchain found via vswhere.
  exit /b 1
)
call "%VSINST%\VC\Auxiliary\Build\vcvarsall.bat" x64
if defined CUDA_PATH if exist "%CUDA_PATH%\bin" set "PATH=%CUDA_PATH%\bin;%PATH%"
%*
