@echo off
setlocal EnableExtensions

set "BACKEND=%~1"
set "OPTION=%~2"
set "BUILD_TYPE=Release"
if "%BACKEND%"=="" set "BACKEND=serial"
if /I "%OPTION%"=="debug" set "BUILD_TYPE=Debug"
if not "%OPTION%"=="" if /I not "%OPTION%"=="debug" if /I not "%OPTION%"=="release" goto usage
if /I "%BACKEND%"=="serial" goto run_one
if /I "%BACKEND%"=="parallel-cpu" goto run_one
if /I "%BACKEND%"=="all" goto run_all
goto usage

:run_all
call :run serial
set "SERIAL_RESULT=%errorlevel%"
call :run parallel-cpu
set "PARALLEL_RESULT=%errorlevel%"
if not "%SERIAL_RESULT%"=="0" exit /b %SERIAL_RESULT%
exit /b %PARALLEL_RESULT%

:run_one
call :run "%BACKEND%"
exit /b %errorlevel%

:run
set "TEST_BACKEND=%~1"
set "BUILD_DIR=build\%TEST_BACKEND%"
if /I "%BUILD_TYPE%"=="Debug" set "BUILD_DIR=%BUILD_DIR%-debug"
call :activate_mkl_runtime
if errorlevel 1 exit /b 1
if /I "%TEST_BACKEND%"=="parallel-cpu" (
    call :activate_parallel_runtime
    if errorlevel 1 exit /b 1
)
call :find_cmake
if errorlevel 1 exit /b 1
"%CMAKE_EXE%" "-DBUILD_DIR:PATH=%CD%\%BUILD_DIR%" "-DBACKEND=%TEST_BACKEND%" "-DBUILD_TYPE=%BUILD_TYPE%" "-DREPORT_FILE:FILEPATH=%CD%\test-results\tests.json" -P tests\run_tests.cmake
exit /b %errorlevel%

:activate_parallel_runtime
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\env\vars.bat" exit /b 1
call "%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\env\vars.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%ProgramFiles%\Microsoft MPI\Bin;%PATH%"
exit /b 0

:activate_mkl_runtime
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\env\vars.bat" exit /b 1
call "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\env\vars.bat" >nul
exit /b %errorlevel%

:find_cmake
set "CMAKE_EXE=cmake.exe"
where cmake.exe >nul 2>nul
if not errorlevel 1 exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto cmake_missing
set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL goto cmake_missing
set "CMAKE_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%CMAKE_EXE%" exit /b 0
:cmake_missing
echo CMake was not found. Run setup.bat first.
exit /b 1

:usage
echo Usage: test.bat [serial^|parallel-cpu^|all] [debug^|release]
echo Tests must already be built with build.bat.
exit /b 1
