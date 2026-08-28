@echo off
setlocal
set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINST="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINST=%%i"
)
if not defined VSINST ( echo ERROR: no VS x64 toolchain & exit /b 1 )
call "%VSINST%\VC\Auxiliary\Build\vcvarsall.bat" x64
if defined CUDA_PATH if exist "%CUDA_PATH%\bin" set "PATH=%CUDA_PATH%\bin;%PATH%"
for %%C in (Release Debug Release Debug Release Debug) do (
  echo ================= CLEAN RUN %%C =================
  rd /s /q "%ROOT%\build\%%C" 2>nul
  cmake -S "%ROOT%" -B "%ROOT%\build\%%C" -G Ninja -DCMAKE_BUILD_TYPE=%%C
  if errorlevel 1 exit /b 1
  cmake --build "%ROOT%\build\%%C"
  if errorlevel 1 exit /b 1
  ctest --test-dir "%ROOT%\build\%%C" --output-on-failure
  if errorlevel 1 exit /b 1
  echo ================= %%C PASSED =================
)
echo ================= MATRIX ALL PASSED =================
