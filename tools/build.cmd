@echo off
rem --- Placement Observatory build driver ---
rem Usage: tools\build.cmd [Debug|Release] [--test] [---extra cmake flags pass through as %2..]
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINST="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINST=%%i"
)
if not defined VSINST (
  echo ERROR: no VS x64 toolchain. Exit 1.
  exit /b 1
)
call "%VSINST%\VC\Auxiliary\Build\vcvarsall.bat" x64
if defined CUDA_PATH if exist "%CUDA_PATH%\bin" set "PATH=%CUDA_PATH%\bin;%PATH%"

echo === Config %CONFIG% ===
cmake -S "%ROOT%" -B "%BUILD%\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% %2 %3 %4 %5 %6 %7 %8
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build "%BUILD%\%CONFIG%"
if errorlevel 1 exit /b 1
echo === CTest ===
ctest --test-dir "%BUILD%\%CONFIG%" --output-on-failure
exit /b %ERRORLEVEL%
