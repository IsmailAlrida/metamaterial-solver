@echo off
setlocal EnableExtensions

set "MODE=build"
set "BACKEND=%~1"
set "GLVIS_REVISION=1b9988ade7b78f125377a3be5c2b8514eafbcf0c"

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

call :patch_glvis
if errorlevel 1 exit /b 1

cmake --preset "%PRESET%"
if errorlevel 1 exit /b %errorlevel%
copy /Y "%BUILD_DIR%\compile_commands.json" "%~dp0compile_commands.json" >nul
if errorlevel 1 exit /b %errorlevel%

if "%MODE%"=="deps" (
    echo LSP configuration completed for %BACKEND%. The app was not built.
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
) else (
    echo Fetching GLVis source into extern\glvis...
    call git.exe clone https://github.com/GLVis/glvis.git extern\glvis
    if errorlevel 1 exit /b 1
)

if not exist extern\glvis\.git exit /b 1
git.exe -C extern\glvis checkout --detach %GLVIS_REVISION%
if errorlevel 1 exit /b 1
exit /b 0

:patch_glvis
if not exist extern\glvis\.git (
    echo GLVis source is missing. Run build.bat deps %BACKEND% first.
    exit /b 1
)

set "GLVIS_PATCH=%~dp0patches\glvis-embedded-window.patch"
git.exe -C extern\glvis apply --reverse --check "%GLVIS_PATCH%" >nul 2>nul
if not errorlevel 1 exit /b 0

git.exe -C extern\glvis apply --check "%GLVIS_PATCH%"
if errorlevel 1 (
    echo GLVis host-window patch does not apply to the downloaded revision.
    exit /b 1
)

git.exe -C extern\glvis apply "%GLVIS_PATCH%"
exit /b %errorlevel%
