@echo off
setlocal

set "BACKEND=%~1"
if "%BACKEND%"=="" set "BACKEND=serial"

if "%BACKEND%"=="deps" (
    set "BACKEND=%~2"
    if "%BACKEND%"=="" set "BACKEND=serial"
    if not "%BACKEND%"=="serial" if not "%BACKEND%"=="serial-cuda" if not "%BACKEND%"=="parallel-cpu" if not "%BACKEND%"=="parallel-cpu-cuda" (
        echo Usage: build.bat deps [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
        exit /b 1
    )
    cmake -S . -B "build-deps-%BACKEND%" -G "Visual Studio 17 2022" -A x64 -DMETAMATERIAL_MFEM_BACKEND=%BACKEND% -DMETAMATERIAL_DEPS_ONLY=ON
    exit /b %errorlevel%
)

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="serial-cuda" if not "%BACKEND%"=="parallel-cpu" if not "%BACKEND%"=="parallel-cpu-cuda" (
    echo Usage: build.bat [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
    echo        build.bat deps [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
    exit /b 1
)

set "BUILD_DIR=build-%BACKEND%"

cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DMETAMATERIAL_MFEM_BACKEND=%BACKEND%
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Debug
