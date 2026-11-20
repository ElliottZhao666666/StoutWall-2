@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "VSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "RUNTIME=%ROOT%\build\win32"
set "FIXTURE=%ROOT%\build\tests\isolated-current-20261121-01\Gallery"
set "OUT=%ROOT%\build\win32\backend-integration-test.exe"
set "OBJ=%ROOT%\build\obj"
if not exist "%VSBUILD%" (echo Visual Studio Build Tools 18 vcvarsall.bat was not found.& exit /b 2)
if not exist "%RUNTIME%\StoutWall.exe" (echo Build the product first with build.cmd.& exit /b 3)
if not exist "%FIXTURE%\Stout_WallGallery_Index.DDT" (echo The isolated gallery fixture is missing.& exit /b 4)
call "%VSBUILD%" x86 10.0.26100.0 -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 5
if not exist "%OBJ%" mkdir "%OBJ%"
set "COMMON=/nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /I"%ROOT%\src""
cl %COMMON% /Fo"%OBJ%\BackendIntegrationProductBackend.obj" /c "%ROOT%\src\ProductBackend.cpp"
if errorlevel 1 exit /b 10
cl %COMMON% /Fo"%OBJ%\BackendIntegrationTest.obj" /c "%ROOT%\tools\backend_integration_test.cpp"
if errorlevel 1 exit /b 11
link /nologo /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 /Brepro /OUT:"%OUT%" "%OBJ%\BackendIntegrationProductBackend.obj" "%OBJ%\BackendIntegrationTest.obj" "%OBJ%\network.lib" "%OBJ%\platform.lib" "%OBJ%\library.lib" windowscodecs.lib winhttp.lib shell32.lib ole32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 12
set "ROUND=%ROOT%\build\tests\backend-integration-%RANDOM%"
"%OUT%" --runtime "%RUNTIME%" --fixture "%FIXTURE%" --root "%ROUND%"
set "CODE=%ERRORLEVEL%"
exit /b %CODE%
