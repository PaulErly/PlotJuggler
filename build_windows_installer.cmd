@echo off
setlocal enabledelayedexpansion

rem Build PlotJuggler and create a Windows installer.
rem
rem Usage:
rem   build_windows_installer.cmd [Debug|Release|RelWithDebInfo]
rem
rem This script expects the Qt Installer Framework to be installed and
rem binarycreator.exe to be on PATH, or passed as the first argument.

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug

set ROOT_DIR=%~dp0
set DATA_DIR=%ROOT_DIR%installer\io.plotjuggler.application\data
set WINDEPLOYQT=%~2
set BINARYCREATOR=%~3

if "%WINDEPLOYQT%"=="" set WINDEPLOYQT=windeployqt.exe
if "%BINARYCREATOR%"=="" set BINARYCREATOR=binarycreator.exe

call "%ROOT_DIR%build_windows_v142.cmd" %BUILD_TYPE%
if errorlevel 1 exit /b 1

if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"

copy /Y "%ROOT_DIR%install_v142\bin\*.*" "%DATA_DIR%" >nul
if errorlevel 1 exit /b 1

call "%ROOT_DIR%installer\windeploy_pj.bat" %WINDEPLOYQT%
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$pyVersion = '3.12.7';" ^
  "$pyXY = ($pyVersion -split '\.')[0..1] -join '.';" ^
  "$pyTag = $pyXY -replace '\.', '';" ^
  "$dataDir = '%DATA_DIR%';" ^
  "$zip = Join-Path $env:TEMP 'python-embed.zip';" ^
  "$url = \"https://www.python.org/ftp/python/$pyVersion/python-$pyVersion-embed-amd64.zip\";" ^
  "Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing;" ^
  "Expand-Archive -Path $zip -DestinationPath $dataDir -Force;" ^
  "Remove-Item (Join-Path $dataDir 'python.exe') -ErrorAction Ignore;" ^
  "Remove-Item (Join-Path $dataDir 'pythonw.exe') -ErrorAction Ignore;" ^
  "$pthFile = Join-Path $dataDir \"python$pyTag._pth\";" ^
  "(Get-Content $pthFile) -replace '^\s*import site', '#import site' | Set-Content $pthFile;" ^
  "$vsRoot = & \"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe\" -latest -property installationPath;" ^
  "$redistRoot = Join-Path $vsRoot 'VC\Redist\MSVC';" ^
  "$crtDir = Get-ChildItem -Path $redistRoot -Recurse -Directory | Where-Object { $_.Name -like 'Microsoft.VC*.CRT' -and $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1;" ^
  "Copy-Item (Join-Path $crtDir.FullName '*.dll') -Destination $dataDir -Force"
if errorlevel 1 exit /b 1

for /f "delims=" %%I in ('where %BINARYCREATOR% 2^>nul') do (
  set BINARYCREATOR_PATH=%%I
  goto :found_binarycreator
)

:found_binarycreator
if "%BINARYCREATOR_PATH%"=="" (
    echo Could not find binarycreator.exe. Pass it as the third argument or add it to PATH.
    exit /b 1
)

set INSTALLER_NAME=PlotJuggler-Windows-installer.exe
echo Creating installer %INSTALLER_NAME%
"%BINARYCREATOR_PATH%" --offline-only -c "%ROOT_DIR%installer\config.xml" -p "%ROOT_DIR%installer" "%ROOT_DIR%%INSTALLER_NAME%"
exit /b %errorlevel%
