@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
if not exist "%ROOT%\build\obj\ProductBackend.obj" (echo Build the product first with build.cmd.& exit /b 2)
if not exist "%ROOT%\build\tests\isolated-current-20261121-01\Gallery\Stout_WallGallery_Index.DDT" (echo The isolated gallery fixture is missing.& exit /b 2)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 10.0.26100.0 -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 2
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I"%ROOT%\src" /Fo"%ROOT%\build\obj\ThemeIntegrationTest.obj" "%ROOT%\tools\theme_integration_test.cpp" "%ROOT%\build\obj\ProductBackend.obj" "%ROOT%\build\obj\network.lib" "%ROOT%\build\obj\platform.lib" "%ROOT%\build\obj\library.lib" /Fe"%ROOT%\build\win32\theme-integration-test.exe" /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 shell32.lib ole32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 3
"%ROOT%\build\win32\theme-integration-test.exe" "%ROOT%\build\win32" "%ROOT%\build\tests\isolated-current-20261121-01\Gallery" "%ROOT%\build\tests\theme-integration-%RANDOM%"
exit /b %ERRORLEVEL%
