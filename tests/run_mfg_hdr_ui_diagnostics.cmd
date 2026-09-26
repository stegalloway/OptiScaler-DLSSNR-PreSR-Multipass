@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "ROOT=%~dp0.."
set "OUT=%TEMP%\mfg_hdr_ui_diagnostics.exe"
cl /nologo /std:c++20 /W4 /WX /EHsc /DNOMINMAX /I"%ROOT%\external\streamline" /I"%ROOT%\OptiScaler" "%ROOT%\tests\mfg_hdr_ui_diagnostics.cpp" /Fe:"%OUT%"
if errorlevel 1 exit /b 1
"%OUT%"
exit /b %ERRORLEVEL%
