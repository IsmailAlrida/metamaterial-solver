@echo off
setlocal

set "BACKEND=%~1"
if "%BACKEND%"=="" set "BACKEND=serial"

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="mpi" if not "%BACKEND%"=="cuda" if not "%BACKEND%"=="mpi-cuda" (
    echo Usage: build.bat [serial^|mpi^|cuda^|mpi-cuda]
    exit /b 1
)

set "BUILD_DIR=build-%BACKEND%"

cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMETAMATERIAL_MFEM_BACKEND=%BACKEND%
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Debug
