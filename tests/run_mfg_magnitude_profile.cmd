@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "ROOT=%~dp0.."
set "OUT=%TEMP%\mfg_magnitude_profile.exe"
cl /nologo /std:c++20 /W4 /WX /EHsc "%ROOT%\tests\mfg_magnitude_profile.cpp" /Fe:"%OUT%"
if errorlevel 1 exit /b 1
"%OUT%"
exit /b %ERRORLEVEL%
