@echo off
setlocal

pushd "%~dp0"
set "SCRIPT_DIR=%CD%"
set "PROJECT_ROOT=%SCRIPT_DIR%\..\..\.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i
if "%VSINSTALL%"=="" exit /b 1
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1

set "OUTPUT_DIR=%SCRIPT_DIR%\x64\Release"
set "OBJECT_DIR=%OUTPUT_DIR%\obj"
if not exist "%OBJECT_DIR%" mkdir "%OBJECT_DIR%"
if errorlevel 1 goto :build_failed

cl /nologo /utf-8 /std:c++20 /EHsc /MD /O2 /FS /LD ^
 /I "%PROJECT_ROOT%\Source\Engine\Core" /I "%PROJECT_ROOT%" ^
 /Fo:"%OBJECT_DIR%\WaterRailShooter0817_release.obj" ^
 "%SCRIPT_DIR%\WaterRailShooter0817.cpp" ^
 /link /OUT:"%OUTPUT_DIR%\WaterRailShooter0817.dll"
if errorlevel 1 goto :build_failed

popd
exit /b 0

:build_failed
set "BUILD_ERROR=%ERRORLEVEL%"
popd
exit /b %BUILD_ERROR%
