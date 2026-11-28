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
if not defined STOUTWALL_OUT set "STOUTWALL_OUT=%ROOT%build\win32"
if not defined STOUTWALL_OBJ set "STOUTWALL_OBJ=%ROOT%build\obj"
set "OUT=%STOUTWALL_OUT%"
set "OBJ=%STOUTWALL_OBJ%"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"
set "COMMON=/nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /Gy /Gw /DSTOUTWALL_STATIC_LINK /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Brepro"
rc /nologo /dSTOUTWALL_DLL_VERSION /fo"%OBJ%\dll-version.res" "%ROOT%resources\app.rc"
if errorlevel 1 exit /b 9

echo [1/6] Building static network, platform and library modules
for %%M in (Network Platform Library) do (
    cl %COMMON% /Fo"%OBJ%\%%M.obj" /c "%ROOT%src\%%M.cpp"
    if errorlevel 1 exit /b 10
    lib /nologo /OUT:"%OBJ%\%%M.lib" "%OBJ%\%%M.obj"
    if errorlevel 1 exit /b 11
)
echo [2/6] Building focused resource-only assets.dll
pushd "%ROOT%resources"
rc /nologo /c65001 /fo"%OBJ%\assets.res" product-assets.rc
if errorlevel 1 (popd& exit /b 13)
link /nologo /dll /noentry /nodefaultlib /noimplib /machine:X86 /Brepro /out:"%OUT%\assets.dll" "%OBJ%\assets.res" "%OBJ%\dll-version.res"
if errorlevel 1 (popd& exit /b 14)
rc /nologo /fo"%OBJ%\app.res" app.rc
if errorlevel 1 (popd& exit /b 15)
popd
echo [3/6] Building decoupled UI and backend libraries
cl %COMMON% /Fo"%OBJ%\ProductBackend.obj" /c "%ROOT%src\ProductBackend.cpp"
if errorlevel 1 exit /b 19
cl %COMMON% /Fo"%OBJ%\ProductUi.obj" /DSTOUTWALL_PRODUCT_UI_ENTRY /c "%ROOT%src\ProductUi.cpp"
if errorlevel 1 exit /b 20
cl %COMMON% /Fo"%OBJ%\ReplicaUi.obj" /c "%ROOT%src\ReplicaUi.cpp"
if errorlevel 1 exit /b 21
cl %COMMON% /Fo"%OBJ%\ProductLayout.obj" /c "%ROOT%src\ProductLayout.cpp"
if errorlevel 1 exit /b 25
cl %COMMON% /Fo"%OBJ%\TaskRunner.obj" /c "%ROOT%src\TaskRunner.cpp"
if errorlevel 1 exit /b 22
cl %COMMON% /Fo"%OBJ%\Assets.obj" /c "%ROOT%src\Assets.cpp"
if errorlevel 1 exit /b 23
cl %COMMON% /Fo"%OBJ%\GalleryWorkbench.obj" /c "%ROOT%src\GalleryWorkbench.cpp"
if errorlevel 1 exit /b 26
lib /nologo /OUT:"%OBJ%\stoutwall-ui.lib" "%OBJ%\ReplicaUi.obj" "%OBJ%\ProductLayout.obj" "%OBJ%\GalleryWorkbench.obj" "%OBJ%\Assets.obj"
if errorlevel 1 exit /b 27
lib /nologo /OUT:"%OBJ%\stoutwall-backend.lib" "%OBJ%\ProductBackend.obj" "%OBJ%\TaskRunner.obj"
if errorlevel 1 exit /b 28
echo [4/6] Linking static-CRT Win7 x86 application
link /nologo /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /Brepro /OPT:REF /OPT:ICF /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%ROOT%resources\app.manifest" /OUT:"%OUT%\StoutWall.exe" "%OBJ%\ProductUi.obj" "%OBJ%\stoutwall-ui.lib" "%OBJ%\stoutwall-backend.lib" "%OBJ%\app.res" "%OBJ%\network.lib" "%OBJ%\platform.lib" "%OBJ%\library.lib" comctl32.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib ole32.lib user32.lib gdi32.lib winhttp.lib advapi32.lib oleaut32.lib uxtheme.lib
if errorlevel 1 exit /b 24
echo [5/6] Staging gallery, original runtime data and static-CRT 7za.exe
copy /Y "%ROOT%dependencies\gallery.dll" "%OUT%\gallery.dll" >nul
if errorlevel 1 exit /b 30
for %%F in (cabarc.exe ForTheme.EDT GalleryBase.EDT) do (
    copy /Y "%RUNTIME%\%%F" "%OUT%\%%F" >nul
    if errorlevel 1 exit /b 31
)
call "%ROOT%tools\build_7za.cmd" "%OUT%"
if errorlevel 1 exit /b 32
rem Remove dependencies staged by older builds; neither is loaded by the product.
echo [6/6] Removing obsolete runtime modules
for %%F in (network.dll platform.dll library.dll 7z.dll cabinet.dll) do if exist "%OUT%\%%F" (
    del /q "%OUT%\%%F"
    if exist "%OUT%\%%F" exit /b 33
)
echo Build succeeded: %OUT%\StoutWall.exe
echo C++ modules and CRT are static. gallery.dll preserves the legacy database format.
exit /b 0
