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

if not exist "%SCRIPT_DIR%\x64\Debug" mkdir "%SCRIPT_DIR%\x64\Debug"

cl /nologo /utf-8 /std:c++20 /EHsc /MDd /Od /Zi /LD /I "%PROJECT_ROOT%\Source\Engine\Core" /I "%PROJECT_ROOT%" "%SCRIPT_DIR%\NewNativeScript.cpp" /Fe:"%SCRIPT_DIR%\x64\Debug\NewNativeScript.dll"
popd
