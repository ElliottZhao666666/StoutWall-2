@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "VSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "OUT=%ROOT%\build\tests\network-smoke-bin"
if not exist "%VSBUILD%" (echo Visual Studio Build Tools 18 vcvarsall.bat was not found.& exit /b 2)
if not exist "%ROOT%\build\win32\network.dll" (echo Build the product first with build.cmd.& exit /b 3)
call "%VSBUILD%" x86 10.0.26100.0 -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 4
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /I"%ROOT%\src" /Fo"%OUT%\network_smoke_test.obj" "%ROOT%\tools\network_smoke_test.cpp" "%ROOT%\build\obj\network.lib" /Fe:"%OUT%\network-smoke-test.exe" /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 /Brepro
if errorlevel 1 exit /b 5
copy /Y "%ROOT%\build\win32\network.dll" "%OUT%\network.dll" >nul
if errorlevel 1 exit /b 6
if not "%~1"=="" goto custom
"%OUT%\network-smoke-test.exe" --root "%ROOT%\build\tests\network-smoke-%RANDOM%"
exit /b %ERRORLEVEL%
:custom
"%OUT%\network-smoke-test.exe" %*
exit /b %ERRORLEVEL%
