@echo off
setlocal enabledelayedexpansion

rem Build PlotJuggler on Windows with the VS v14.29 toolset.
rem
rem Usage:
rem   build_windows_v142.cmd [Debug|Release|RelWithDebInfo]
rem
rem The script configures CMake, builds PlotJuggler and DataLoadMDF, and
rem installs the result into install_v142\.

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug

set ROOT_DIR=%~dp0
set BUILD_DIR=%ROOT_DIR%build_v142
set INSTALL_DIR=%ROOT_DIR%install_v142

set VS_VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist %VS_VCVARS% (
    echo Could not find vcvars64.bat at:
    echo   %VS_VCVARS%
    exit /b 1
)

call %VS_VCVARS% -vcvars_ver=14.29
if errorlevel 1 exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" ^
      -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
      -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
      -DPJ_PLUGINS_DIRECTORY="%INSTALL_DIR%/lib/plotjuggler/plugins"
    if errorlevel 1 exit /b 1
)

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --target plotjuggler DataLoadMDF install
exit /b %errorlevel%
