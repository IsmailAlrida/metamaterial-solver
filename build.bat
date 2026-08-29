@echo off
setlocal EnableExtensions

set "MODE=build"
set "BACKEND=%~1"
set "CUDA_ARCH=%~2"
set "GLVIS_REVISION=1b9988ade7b78f125377a3be5c2b8514eafbcf0c"

if /I "%BACKEND%"=="deps" (
    set "MODE=deps"
    set "BACKEND=%~2"
    set "CUDA_ARCH=%~3"
)

if "%BACKEND%"=="" set "BACKEND=serial"

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="serial-cuda" if not "%BACKEND%"=="parallel-cpu" if not "%BACKEND%"=="parallel-cpu-cuda" (
    echo Usage: build.bat [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda] [CUDA architecture]
    echo        build.bat deps [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda] [CUDA architecture]
    exit /b 1
)

call :activate_msvc
if errorlevel 1 exit /b 1

if not "%BACKEND:cuda=%"=="%BACKEND%" (
    call :check_cuda
    if errorlevel 1 exit /b 1
)

if defined CUDA_ARCH if "%BACKEND:cuda=%"=="%BACKEND%" (
    echo A CUDA architecture can only be supplied for a CUDA backend.
    exit /b 1
)

set "BUILD_ROOT=build"
set "BUILD_DIR=%BUILD_ROOT%\%BACKEND%"
set "PRESET=%BACKEND%"
if "%MODE%"=="deps" set "PRESET=deps-%BACKEND%"

echo Backend: %BACKEND%
echo Build dir: %BUILD_DIR%
echo Deps source dir: %BUILD_ROOT%\deps\src
echo Preset: %PRESET%
if defined CUDA_ARCH echo CUDA architecture: %CUDA_ARCH%

if "%MODE%"=="deps" (
    call :fetch_glvis
    if errorlevel 1 exit /b 1
)

call :patch_glvis
if errorlevel 1 exit /b 1

if defined CUDA_ARCH (
    cmake --preset "%PRESET%" -DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCH%
) else (
    cmake --preset "%PRESET%"
)
if errorlevel 1 exit /b %errorlevel%
copy /Y "%BUILD_DIR%\compile_commands.json" "%~dp0compile_commands.json" >nul
if errorlevel 1 exit /b %errorlevel%

if "%MODE%"=="deps" (
    echo LSP configuration completed for %BACKEND%. The app was not built.
    exit /b 0
)

cmake --build --preset "%BACKEND%"
exit /b %errorlevel%

:activate_msvc
where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto find_msvc
echo Visual Studio C++ tools were not found. Run setup.bat first.
exit /b 1

:find_msvc
set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if defined VS_INSTALL goto load_msvc
echo Visual Studio C++ tools were not found. Run setup.bat first.
exit /b 1

:load_msvc
call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
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
