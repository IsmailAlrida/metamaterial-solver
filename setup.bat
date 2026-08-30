@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "MODE=%~1"
if "%MODE%"=="" set "MODE=install"

if /I "%MODE%"=="check" goto check
if /I "%MODE%"=="help" goto usage
if /I not "%MODE%"=="install" goto usage

net session >nul 2>nul
if errorlevel 1 (
    echo Requesting administrator access for the native SDK installers...
    powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%~f0' -ArgumentList install -WorkingDirectory '%CD%'"
    exit /b %errorlevel%
)

where winget.exe >nul 2>nul
if errorlevel 1 (
    echo winget.exe is missing. Install Microsoft App Installer, then run setup.bat again.
    exit /b 1
)

call :ensure_tool git.exe Git.Git Git
if errorlevel 1 exit /b 1
call :ensure_tool cmake.exe Kitware.CMake CMake
if errorlevel 1 exit /b 1
call :ensure_tool ninja.exe Ninja-build.Ninja Ninja
if errorlevel 1 exit /b 1
call :ensure_visual_studio
if errorlevel 1 exit /b 1
if exist "%ProgramFiles%\Microsoft MPI\Bin\mpiexec.exe" (
    echo Microsoft MPI runtime is already available.
) else (
    call :ensure_package Microsoft.msmpi "Microsoft MPI runtime"
    if errorlevel 1 exit /b 1
)
call :ensure_mpi_sdk
if errorlevel 1 exit /b 1
call :ensure_mkl
if errorlevel 1 exit /b 1
call :record_sdk_environment

echo.
echo Setup completed. Open a new terminal, then run:
echo   setup.bat check
echo   build.bat deps serial
echo.
pause
exit /b 0

:ensure_tool
where %~1 >nul 2>nul
if not errorlevel 1 (
    echo %~3 is already available.
    exit /b 0
)
call :ensure_package %~2 "%~3"
exit /b %errorlevel%

:ensure_package
echo Installing %~2...
winget.exe install --exact --id %~1 --source winget --accept-package-agreements --accept-source-agreements --silent --disable-interactivity
if errorlevel 1 (
    echo Failed to install %~2.
    exit /b 1
)
exit /b 0

:ensure_visual_studio
call :find_visual_studio
if defined VS_INSTALL (
    echo Visual Studio C++ tools are already available.
    exit /b 0
)
echo Installing Visual Studio 2022 C++ build tools...
winget.exe install --exact --id Microsoft.VisualStudio.2022.BuildTools --source winget --accept-package-agreements --accept-source-agreements --silent --disable-interactivity --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
if errorlevel 1 (
    echo Failed to install the Visual Studio C++ build tools.
    exit /b 1
)
exit /b 0

:ensure_mpi_sdk
if not exist "%ProgramFiles(x86)%\Microsoft SDKs\MPI\Include\mpi.h" goto install_mpi_sdk
if not exist "%ProgramFiles(x86)%\Microsoft SDKs\MPI\Lib\x64\msmpi.lib" goto install_mpi_sdk
echo Microsoft MPI SDK is already available.
exit /b 0

:install_mpi_sdk
set "SETUP_TEMP=%TEMP%\metamaterial-solver-setup"
set "MPI_SDK_MSI=%SETUP_TEMP%\msmpisdk.msi"
if not exist "%SETUP_TEMP%" mkdir "%SETUP_TEMP%"

echo Downloading the official Microsoft MPI SDK 10.1.3...
curl.exe --fail --location --output "%MPI_SDK_MSI%" "https://download.microsoft.com/download/7/2/7/72731ebb-b63c-4170-ade7-836966263a8f/msmpisdk.msi"
if errorlevel 1 exit /b 1

powershell.exe -NoProfile -Command "$signature = Get-AuthenticodeSignature -LiteralPath '%MPI_SDK_MSI%'; if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft') { exit 1 }"
if errorlevel 1 (
    echo The Microsoft MPI SDK signature check failed. The installer was not run.
    exit /b 1
)

msiexec.exe /i "%MPI_SDK_MSI%" /quiet /norestart
if errorlevel 1 (
    echo Failed to install the Microsoft MPI SDK.
    exit /b 1
)
exit /b 0

:ensure_mkl
if exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\include\mkl.h" goto mkl_available
call :ensure_package Intel.oneMKL "Intel oneMKL"
exit /b %errorlevel%

:mkl_available
echo Intel oneMKL is already available.
exit /b 0

:record_sdk_environment
set "MPI_INC=%ProgramFiles(x86)%\Microsoft SDKs\MPI\Include"
set "MPI_LIB64=%ProgramFiles(x86)%\Microsoft SDKs\MPI\Lib\x64"
if exist "%MPI_INC%\mpi.h" setx.exe /M MSMPI_INC "%MPI_INC%" >nul
if exist "%MPI_LIB64%\msmpi.lib" setx.exe /M MSMPI_LIB64 "%MPI_LIB64%" >nul

set "MKL_PATH=%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest"
if exist "%MKL_PATH%\include\mkl.h" (
    setx.exe /M MKLROOT "%MKL_PATH%" >nul
    powershell.exe -NoProfile -Command "$mklBin = '%MKL_PATH%\bin'; $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine'); if (($machinePath -split ';') -notcontains $mklBin) { [Environment]::SetEnvironmentVariable('Path', $machinePath.TrimEnd(';') + ';' + $mklBin, 'Machine') }"
)
exit /b 0

:check
set "MISSING=0"
echo Checking native build prerequisites...
call :check_tool git.exe "%ProgramFiles%\Git\cmd\git.exe" Git
call :check_tool cmake.exe "%ProgramFiles%\CMake\bin\cmake.exe" CMake
call :check_tool ninja.exe "%LOCALAPPDATA%\Microsoft\WinGet\Links\ninja.exe" Ninja

call :find_visual_studio
if defined VS_INSTALL (
    echo [ok] Visual Studio C++ tools: !VS_INSTALL!
) else (
    echo [missing] Visual Studio C++ tools
    set /a MISSING+=1
)

call :check_file "%ProgramFiles%\Microsoft MPI\Bin\mpiexec.exe" "Microsoft MPI runtime"
call :check_file "%ProgramFiles(x86)%\Microsoft SDKs\MPI\Include\mpi.h" "Microsoft MPI SDK headers"
call :check_file "%ProgramFiles(x86)%\Microsoft SDKs\MPI\Lib\x64\msmpi.lib" "Microsoft MPI SDK x64 library"
call :check_file "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\include\mkl.h" "Intel oneMKL headers"
call :check_file "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\lib\mkl_core.lib" "Intel oneMKL libraries"
call :check_file "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\bin\mkl_sequential.3.dll" "Intel oneMKL runtime"

if not "!MISSING!"=="0" (
    echo.
    echo !MISSING! required prerequisite^(s^) missing. Run setup.bat to install them.
    exit /b 1
)
echo All required native prerequisites are available.
exit /b 0

:find_visual_studio
set "VS_INSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
exit /b 0

:check_tool
where %~1 >nul 2>nul
if not errorlevel 1 goto check_tool_path
if exist "%~2" goto check_tool_fallback
echo [missing] %~3
set /a MISSING+=1
exit /b 0

:check_tool_path
for /f "delims=" %%I in ('where %~1') do echo [ok] %~3: %%I
exit /b 0

:check_tool_fallback
echo [ok] %~3: %~2
exit /b 0

:check_file
if exist "%~1" goto check_file_found
echo [missing] %~2
set /a MISSING+=1
exit /b 0

:check_file_found
echo [ok] %~2: %~1
exit /b 0

:usage
echo Usage: setup.bat [install^|check^|help]
echo.
echo   install  Install the Windows C++ toolchain, MS-MPI, and oneMKL.
echo   check    Check prerequisites without changing the machine.
exit /b 1
