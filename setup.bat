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
if errorlevel 1 goto failed
call :ensure_visual_studio
if errorlevel 1 goto failed
call :ensure_tool cmake.exe Kitware.CMake CMake "%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if errorlevel 1 goto failed
call :ensure_tool ninja.exe Ninja-build.Ninja Ninja "%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if errorlevel 1 goto failed
call :ensure_msys2
if errorlevel 1 goto failed
if exist "%ProgramFiles%\Microsoft MPI\Bin\mpiexec.exe" (
    echo Microsoft MPI runtime is already available.
) else (
    call :ensure_package Microsoft.msmpi "Microsoft MPI runtime"
    if errorlevel 1 goto failed
)
call :ensure_mpi_sdk
if errorlevel 1 goto failed
call :ensure_mkl
if errorlevel 1 goto failed
call :ensure_fortran
if errorlevel 1 goto failed
call :record_sdk_environment

echo.
echo Setup completed. Open a new terminal, then run:
echo   setup.bat check
echo   build.bat deps serial
echo   build.bat deps parallel-cpu
echo.
pause
exit /b 0

:failed
echo.
echo Setup failed. Fix the reported prerequisite, then run setup.bat again.
pause
exit /b 1

:ensure_tool
where %~1 >nul 2>nul
if not errorlevel 1 (
    echo %~3 is already available.
    exit /b 0
)
if exist "%~4" (
    echo %~3 is already available through Visual Studio.
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

:ensure_msys2
set "MSYS2_ROOT=C:\msys64"
set "MSYS2_BASH=%MSYS2_ROOT%\usr\bin\bash.exe"
if exist "%MSYS2_BASH%" goto install_msys2_packages
call :ensure_package MSYS2.MSYS2 "MSYS2"
if errorlevel 1 exit /b 1
if not exist "%MSYS2_BASH%" (
    echo MSYS2 installed, but %MSYS2_BASH% was not found.
    exit /b 1
)

:install_msys2_packages
echo Ensuring the Ipopt MSYS2 build tools are available...
"%MSYS2_BASH%" -lc "pacman -S --needed --noconfirm binutils diffutils git grep make patch pkgconf"
if errorlevel 1 (
    echo Failed to install the Ipopt MSYS2 build tools.
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
call :find_visual_studio
if not defined VS_INSTALL exit /b 1
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
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\include\mkl.h" goto install_mkl
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\lib\mkl_core.lib" goto install_mkl
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\bin\mkl_sequential.3.dll" goto install_mkl
goto mkl_available

:install_mkl
call :ensure_package Intel.oneMKL "Intel oneMKL"
if errorlevel 1 exit /b 1
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\lib\mkl_core.lib" goto incomplete_mkl
if not exist "%ProgramFiles(x86)%\Intel\oneAPI\mkl\latest\bin\mkl_sequential.3.dll" goto incomplete_mkl
exit /b 0

:incomplete_mkl
echo Intel oneMKL is installed without the required libraries or runtime.
echo Repair the Intel oneMKL installation, then run setup.bat again.
exit /b 1

:mkl_available
echo Intel oneMKL is already available.
exit /b 0

:ensure_fortran
if exist "%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\bin\ifx.exe" (
    echo Intel Fortran Compiler is already available.
    exit /b 0
)
call :ensure_package Intel.FortranCompiler "Intel Fortran Compiler"
if errorlevel 1 exit /b 1
if exist "%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\bin\ifx.exe" exit /b 0
echo Intel Fortran Compiler installed, but ifx.exe was not found.
echo Repair the Intel Fortran Compiler installation, then run setup.bat again.
exit /b 1

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
set "COMPILER_PATH=%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest"
if exist "%COMPILER_PATH%\bin\ifx.exe" powershell.exe -NoProfile -Command "$compilerBin = '%COMPILER_PATH%\bin'; $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine'); if (($machinePath -split ';') -notcontains $compilerBin) { [Environment]::SetEnvironmentVariable('Path', $machinePath.TrimEnd(';') + ';' + $compilerBin, 'Machine') }"
exit /b 0

:check
set "MISSING=0"
echo Checking native build prerequisites...
call :find_visual_studio
call :check_tool git.exe "%ProgramFiles%\Git\cmd\git.exe" Git
call :check_tool cmake.exe "!VS_INSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" CMake
call :check_tool ninja.exe "!VS_INSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" Ninja

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
call :check_file "%ProgramFiles(x86)%\Intel\oneAPI\compiler\latest\bin\ifx.exe" "Intel Fortran Compiler"
call :check_file "C:\msys64\usr\bin\bash.exe" "MSYS2 bash"
call :check_file "C:\msys64\usr\bin\cygpath.exe" "MSYS2 cygpath"
call :check_file "C:\msys64\usr\bin\make.exe" "MSYS2 make"
call :check_file "C:\msys64\usr\bin\patch.exe" "MSYS2 patch"
if exist "C:\msys64\usr\bin\pkg-config.exe" (
    echo [ok] MSYS2 pkg-config
) else if exist "C:\msys64\usr\bin\pkgconf.exe" (
    echo [ok] MSYS2 pkg-config
) else (
    echo [missing] MSYS2 pkg-config
    set /a MISSING+=1
)

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
echo   install  Install the Windows C++/Fortran toolchain, MSYS2, MS-MPI, and oneMKL.
echo   check    Check prerequisites without changing the machine.
exit /b 1
