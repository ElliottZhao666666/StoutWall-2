@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
if not defined STOUTWALL_VSBUILD set "STOUTWALL_VSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not defined STOUTWALL_SDK set "STOUTWALL_SDK=10.0.26100.0"
if not defined STOUTWALL_MSVC_VERSION set "STOUTWALL_MSVC_VERSION=14.44"
set "RUNTIME=%ROOT%dependencies\runtime"
if not exist "%STOUTWALL_VSBUILD%" (echo Set STOUTWALL_VSBUILD to an installed vcvarsall.bat.& exit /b 2)
for %%F in (cabarc.exe ForTheme.EDT GalleryBase.EDT) do (
    if not exist "%RUNTIME%\%%F" (echo Missing repository dependency: %%F& exit /b 3)
)
call "%STOUTWALL_VSBUILD%" x86 %STOUTWALL_SDK% -vcvars_ver=%STOUTWALL_MSVC_VERSION% >nul
if errorlevel 1 exit /b 4
set "OUT=%ROOT%build\win32"
set "OBJ=%ROOT%build\obj"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"
set "COMMON=/nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Brepro"

echo [1/6] Building static-CRT network.dll
cl %COMMON% /Fo"%OBJ%\Network.obj" /DSWN_BUILD /LD "%ROOT%src\Network.cpp" /link /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /Brepro /IMPLIB:"%OBJ%\network.lib" /OUT:"%OUT%\network.dll" winhttp.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b 10
echo [2/6] Building static-CRT platform.dll
cl %COMMON% /Fo"%OBJ%\Platform.obj" /DSWP_BUILD /LD "%ROOT%src\Platform.cpp" /link /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /Brepro /IMPLIB:"%OBJ%\platform.lib" /OUT:"%OUT%\platform.dll" shell32.lib ole32.lib user32.lib
if errorlevel 1 exit /b 11
echo [3/6] Building static-CRT library.dll
cl %COMMON% /Fo"%OBJ%\Library.obj" /LD "%ROOT%src\Library.cpp" /link /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /Brepro /IMPLIB:"%OBJ%\library.lib" /OUT:"%OUT%\library.dll"
if errorlevel 1 exit /b 12
echo [4/6] Building resource-only assets.dll
pushd "%ROOT%resources"
rc /nologo /c65001 /fo"%OBJ%\assets.res" assets.rc
if errorlevel 1 (popd& exit /b 13)
link /nologo /dll /noentry /nodefaultlib /noimplib /machine:X86 /Brepro /out:"%OUT%\assets.dll" "%OBJ%\assets.res"
if errorlevel 1 (popd& exit /b 14)
rc /nologo /fo"%OBJ%\app.res" app.rc
if errorlevel 1 (popd& exit /b 15)
popd
echo [5/6] Building static-CRT Win7 x86 application
cl %COMMON% /Fo"%OBJ%\ProductBackend.obj" /c "%ROOT%src\ProductBackend.cpp"
if errorlevel 1 exit /b 19
cl %COMMON% /Fo"%OBJ%\ProductUi.obj" /DSTOUTWALL_PRODUCT_UI_ENTRY /c "%ROOT%src\ProductUi.cpp"
if errorlevel 1 exit /b 20
cl %COMMON% /Fo"%OBJ%\ReplicaUi.obj" /c "%ROOT%src\ReplicaUi.cpp"
if errorlevel 1 exit /b 21
cl %COMMON% /Fo"%OBJ%\TaskRunner.obj" /c "%ROOT%src\TaskRunner.cpp"
if errorlevel 1 exit /b 22
cl %COMMON% /Fo"%OBJ%\Assets.obj" /c "%ROOT%src\Assets.cpp"
if errorlevel 1 exit /b 23
link /nologo /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /Brepro /OUT:"%OUT%\StoutWall.exe" "%OBJ%\ProductBackend.obj" "%OBJ%\ProductUi.obj" "%OBJ%\ReplicaUi.obj" "%OBJ%\TaskRunner.obj" "%OBJ%\Assets.obj" "%OBJ%\app.res" "%OBJ%\network.lib" "%OBJ%\platform.lib" "%OBJ%\library.lib" comctl32.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib ole32.lib user32.lib gdi32.lib winhttp.lib advapi32.lib gdi32.lib oleaut32.lib
if errorlevel 1 exit /b 24
echo [6/6] Staging gallery, original runtime data and static-CRT 7za.exe
copy /Y "%ROOT%dependencies\gallery.dll" "%OUT%\gallery.dll" >nul
if errorlevel 1 exit /b 30
for %%F in (cabarc.exe ForTheme.EDT GalleryBase.EDT) do (
    copy /Y "%RUNTIME%\%%F" "%OUT%\%%F" >nul
    if errorlevel 1 exit /b 31
)
call "%ROOT%tools\build_7za.cmd" "%OUT%"
if errorlevel 1 exit /b 32
rem Remove dependencies staged by older builds; neither is loaded by the product.
for %%F in (7z.dll cabinet.dll) do if exist "%OUT%\%%F" (
    del /q "%OUT%\%%F"
    if exist "%OUT%\%%F" exit /b 33
)
echo Build succeeded: %OUT%\StoutWall.exe
echo Static CRT: /MT for EXE and all code DLLs; assets.dll is resource-only.
exit /b 0
