@echo off
setlocal EnableExtensions

set "MODE=build"
set "BUILD_TYPE=Release"
set "BUILD_TESTS=OFF"
set "JOBS="
set "GLVIS_REVISION=1b9988ade7b78f125377a3be5c2b8514eafbcf0c"

if /I "%~1"=="deps" (
    set "MODE=deps"
    shift
)

set "BACKEND=%~1"
if not "%~1"=="" shift
if "%BACKEND%"=="" set "BACKEND=serial"

:parse_options
if "%~1"=="" goto options_done
if /I "%~1"=="debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto parse_options
)
if /I "%~1"=="release" (
    set "BUILD_TYPE=Release"
    shift
    goto parse_options
)
if /I "%~1"=="tests" (
    set "BUILD_TESTS=ON"
    shift
    goto parse_options
)
if /I "%~1"=="-j" goto parse_jobs
if /I "%~1"=="--jobs" goto parse_jobs
echo Unexpected build option: %~1
exit /b 1

:parse_jobs
shift
if "%~1"=="" (
    echo --jobs requires a positive integer.
    exit /b 1
)
set "JOBS=%~1"
shift
goto parse_options

:options_done
if not defined JOBS if defined CMAKE_BUILD_PARALLEL_LEVEL set "JOBS=%CMAKE_BUILD_PARALLEL_LEVEL%"
if not defined JOBS set "JOBS=4"
for /f "delims=0123456789" %%I in ("%JOBS%") do (
    echo Build jobs must be a positive integer, got: %JOBS%
    exit /b 1
)
if "%JOBS%"=="0" (
    echo Build jobs must be greater than zero.
    exit /b 1
)

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="parallel-cpu" (
    echo Usage: build.bat [serial^|parallel-cpu] [debug] [tests] [-j N]
    echo        build.bat deps [serial^|parallel-cpu] [debug] [tests] [-j N]
    exit /b 1
)

call :activate_msvc
if errorlevel 1 exit /b 1
call :activate_oneapi
if errorlevel 1 exit /b 1

set "BUILD_ROOT=build"
set "BUILD_DIR=%BUILD_ROOT%\%BACKEND%"
set "PRESET=%BACKEND%"
if /I "%BUILD_TYPE%"=="Debug" (
    set "BUILD_DIR=%BUILD_DIR%-debug"
    set "PRESET=%PRESET%-debug"
)
if "%MODE%"=="deps" set "PRESET=deps-%BACKEND%"
if "%MODE%"=="deps" if /I "%BUILD_TYPE%"=="Debug" set "PRESET=%PRESET%-debug"

echo Backend: %BACKEND%
echo Build type: %BUILD_TYPE%
echo Build dir: %BUILD_DIR%
echo Deps source dir: %BUILD_ROOT%\deps\src
echo Preset: %PRESET%
echo Build jobs: %JOBS%
echo Project tests: %BUILD_TESTS%

if "%MODE%"=="deps" (
    call :fetch_glvis
    if errorlevel 1 exit /b 1
)

call :patch_glvis
if errorlevel 1 exit /b 1

cmake --preset "%PRESET%" -DMETAMATERIAL_BUILD_TESTS=%BUILD_TESTS% -DMETAMATERIAL_DEPENDENCY_JOBS=%JOBS%
if errorlevel 1 exit /b %errorlevel%
copy /Y "%BUILD_DIR%\compile_commands.json" "%~dp0compile_commands.json" >nul
if errorlevel 1 exit /b %errorlevel%

if "%MODE%"=="deps" (
    cmake --build "%BUILD_DIR%" --target optimizer_dependencies --parallel %JOBS%
    if errorlevel 1 exit /b 1
    echo Dependency build completed for %BACKEND%. The app was not built.
    exit /b 0
)

if /I "%BUILD_TYPE%"=="Debug" (
    cmake --build --preset "%BACKEND%-debug" --parallel %JOBS%
) else (
    cmake --build --preset "%BACKEND%" --parallel %JOBS%
)
exit /b %errorlevel%

:activate_msvc
where cl.exe >nul 2>nul
if not errorlevel 1 if defined INCLUDE exit /b 0

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

:activate_oneapi
set "ONEAPI_COMPILER_VARS=%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\env\vars.bat"
set "ONEAPI_MKL_VARS=%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\env\vars.bat"
if not exist "%ONEAPI_COMPILER_VARS%" (
    echo Intel oneAPI environment is missing. Run setup.bat first.
    exit /b 1
)
if not exist "%ONEAPI_MKL_VARS%" (
    echo Intel oneMKL environment is missing. Run setup.bat first.
    exit /b 1
)
call "%ONEAPI_COMPILER_VARS%" >nul
if errorlevel 1 exit /b %errorlevel%
call "%ONEAPI_MKL_VARS%" >nul
if errorlevel 1 exit /b %errorlevel%
set "MSMPI_INC=%ProgramFiles(x86)%\Microsoft SDKs\MPI\Include"
set "MSMPI_LIB64=%ProgramFiles(x86)%\Microsoft SDKs\MPI\Lib\x64"
set "PATH=%ProgramFiles%\Microsoft MPI\Bin;%PATH%"
if /I not "%BACKEND%"=="parallel-cpu" exit /b 0
where ifx.exe >nul 2>nul
if errorlevel 1 (
    echo Intel Fortran Compiler is missing. Run setup.bat first.
    exit /b 1
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
git.exe -C extern\glvis apply --reverse --check --ignore-space-change --ignore-whitespace "%GLVIS_PATCH%" >nul 2>nul
if not errorlevel 1 exit /b 0

git.exe -C extern\glvis apply --check --ignore-space-change --ignore-whitespace "%GLVIS_PATCH%"
if errorlevel 1 (
    echo GLVis host-window patch does not apply to the downloaded revision.
    exit /b 1
)

git.exe -C extern\glvis apply --ignore-space-change --ignore-whitespace "%GLVIS_PATCH%"
exit /b %errorlevel%
