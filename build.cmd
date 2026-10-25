@echo off
setlocal
set "PLAGUE_VS_INSTALL="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PLAGUE_VS_INSTALL=%%i"
if not defined PLAGUE_VS_INSTALL (
  echo Visual Studio C++ tools are required.
  exit /b 1
)
call "%PLAGUE_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined OPENXR_SDK_SOURCE set "OPENXR_SDK_SOURCE=%~dp0vendor\openxr-sdk-source"
cmake -S "%~dp0." -B "%~dp0build-release" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOPENXR_SDK_SOURCE="%OPENXR_SDK_SOURCE%"
if errorlevel 1 exit /b 1
if "%~1"=="" (
  cmake --build "%~dp0build-release" --target PlagueStereoProbe PlagueVRHost PlagueProbeLoader
) else (
  cmake --build "%~dp0build-release" --target "%~1"
)
