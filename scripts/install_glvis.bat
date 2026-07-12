@echo off
setlocal

set "BACKEND=%~1"
if "%BACKEND%"=="" set "BACKEND=serial"

if not "%BACKEND%"=="serial" if not "%BACKEND%"=="serial-cuda" if not "%BACKEND%"=="parallel-cpu" if not "%BACKEND%"=="parallel-cpu-cuda" (
    echo Usage: scripts\install_glvis.bat [serial^|serial-cuda^|parallel-cpu^|parallel-cpu-cuda]
    exit /b 1
)

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul

set "MFEM_DIR=%CD%\build\%BACKEND%\deps\mfem-build"
if not exist "%MFEM_DIR%\MFEMConfig.cmake" (
    echo Missing "%MFEM_DIR%\MFEMConfig.cmake"
    echo Run build.bat %BACKEND% first.
    popd >nul
    exit /b 1
)

if not defined VCPKG_ROOT (
    for /f "delims=" %%I in ('where vcpkg 2^>nul') do set "VCPKG_EXE=%%I"
) else (
    set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
)

if not exist "%VCPKG_EXE%" (
    echo GLVis native Windows builds need vcpkg dependencies.
    echo Install vcpkg and set VCPKG_ROOT, then rerun this script.
    popd >nul
    exit /b 1
)

"%VCPKG_EXE%" install fontconfig freetype sdl2 glew glm libpng --triplet=x64-windows
if errorlevel 1 (
    popd >nul
    exit /b %errorlevel%
)

if not exist extern mkdir extern
if not exist extern\glvis (
    call git.exe clone https://github.com/GLVis/glvis.git extern\glvis
    if errorlevel 1 (
        popd >nul
        exit /b 1
    )
    if not exist extern\glvis\.git (
        popd >nul
        exit /b 1
    )
)

for %%I in ("%VCPKG_EXE%") do set "VCPKG_ROOT=%%~dpI"
set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

cmake -S extern\glvis -B "build\glvis-%BACKEND%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
    -DCMAKE_INSTALL_PREFIX="%CD%\tools\glvis-%BACKEND%" ^
    -DMFEM_DIR="%MFEM_DIR%" ^
    -DGLVIS_USE_LIBTIFF=OFF ^
    -DGLVIS_USE_LIBPNG=ON ^
    -DGLVIS_USE_EGL=OFF
if errorlevel 1 (
    popd >nul
    exit /b %errorlevel%
)

cmake --build "build\glvis-%BACKEND%" --config Release --target INSTALL
set "RESULT=%errorlevel%"

popd >nul
exit /b %RESULT%
