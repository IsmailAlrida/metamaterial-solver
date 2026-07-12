@echo off
setlocal EnableExtensions

set "MODE=build"
set "BACKEND=%~1"

if /I "%BACKEND%"=="deps" (
    set "MODE=deps"
    set "BACKEND=%~2"
)

if "%BACKEND%"=="" set "BACKEND=serial"

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="serial-cuda" if not "%BACKEND%"=="parallel-cpu" if not "%BACKEND%"=="parallel-cpu-cuda" (
    echo Usage: build.bat [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
    echo        build.bat deps [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
    exit /b 1
)

if not "%BACKEND:cuda=%"=="%BACKEND%" (
    call :check_cuda
    if errorlevel 1 exit /b 1
)

set "BUILD_ROOT=build"
set "BUILD_DIR=%BUILD_ROOT%\%BACKEND%"
set "PRESET=%BACKEND%"
if "%MODE%"=="deps" set "PRESET=deps-%BACKEND%"

echo Backend: %BACKEND%
echo Build dir: %BUILD_DIR%
echo Deps source dir: %BUILD_ROOT%\deps\src
echo Preset: %PRESET%

if "%MODE%"=="deps" (
    call :fetch_glvis
    if errorlevel 1 exit /b 1
)

cmake --preset "%PRESET%"
if errorlevel 1 exit /b %errorlevel%

if "%MODE%"=="deps" (
    echo Dependency configure completed for %BACKEND%.
    exit /b 0
)

cmake --build --preset "%BACKEND%"
exit /b %errorlevel%

:check_cuda
where nvcc >nul 2>nul
if errorlevel 1 (
    echo CUDA Toolkit not found: nvcc.exe is not on PATH.
    echo Open a CUDA-enabled Developer Command Prompt or install the NVIDIA CUDA Toolkit.
    exit /b 1
)

where nvidia-smi >nul 2>nul
if errorlevel 1 (
    echo Warning: nvidia-smi.exe is not on PATH. CUDA configure may still work, but driver/GPU detection is unavailable.
)
exit /b 0

:fetch_glvis
if not exist extern mkdir extern
if exist extern\glvis (
    echo GLVis source already exists at extern\glvis.
    exit /b 0
)

echo Fetching GLVis source into extern\glvis...
call git.exe clone https://github.com/GLVis/glvis.git extern\glvis
if errorlevel 1 exit /b 1
if not exist extern\glvis\.git exit /b 1
exit /b 0
