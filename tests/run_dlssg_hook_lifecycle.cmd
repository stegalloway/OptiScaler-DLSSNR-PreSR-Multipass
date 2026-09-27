@echo off
setlocal
if not defined ProgramFiles(x86) set "ProgramFiles(x86)=C:\Program Files (x86)"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0\.."
cl /nologo /std:c++20 /EHsc /DNOMINMAX tests\dlssg_hook_lifecycle.cpp /Fe:"%TEMP%\dlssg_hook_lifecycle.exe"
if errorlevel 1 exit /b 1
"%TEMP%\dlssg_hook_lifecycle.exe"
set rc=%errorlevel%
popd
exit /b %rc%
