@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\src"
set "LABEL=current"
set "DEFINES=/DSTOUTWALL_PREVIEW_TEST /DSTOUTWALL_STATIC_LINK"
set "UI_SOURCES="%SOURCE%\ProductLayout.cpp" "%SOURCE%\GalleryWorkbench.cpp""
if not "%~1"=="" (
    set "SOURCE=%~1"
    set "LABEL=baseline"
    set "DEFINES=/DSTOUTWALL_PREVIEW_TEST /DSTOUTWALL_PREVIEW_BASELINE"
    set "UI_SOURCES="
)
if not defined STOUTWALL_VSBUILD set "STOUTWALL_VSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not defined STOUTWALL_SDK set "STOUTWALL_SDK=10.0.26100.0"
if not defined STOUTWALL_MSVC_VERSION set "STOUTWALL_MSVC_VERSION=14.44"
if not exist "%STOUTWALL_VSBUILD%" (echo Set STOUTWALL_VSBUILD to an installed vcvarsall.bat.& exit /b 2)
call "%STOUTWALL_VSBUILD%" x86 %STOUTWALL_SDK% -vcvars_ver=%STOUTWALL_MSVC_VERSION% >nul
if errorlevel 1 exit /b 2
if not defined STOUTWALL_OUT set "STOUTWALL_OUT=%ROOT%\build\win32"
if not defined STOUTWALL_OBJ set "STOUTWALL_OBJ=%ROOT%\build\obj"
set "OUT=%STOUTWALL_OUT%\preview-benchmark-%LABEL%.exe"
set "OBJ=%STOUTWALL_OBJ%\preview-%LABEL%"
if not exist "%OBJ%" mkdir "%OBJ%"
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 %DEFINES% /I"%SOURCE%" /Fo"%OBJ%\\" "%ROOT%\tools\preview_benchmark.cpp" "%SOURCE%\ProductBackend.cpp" "%SOURCE%\ReplicaUi.cpp" "%SOURCE%\Assets.cpp" %UI_SOURCES% "%STOUTWALL_OBJ%\network.lib" "%STOUTWALL_OBJ%\platform.lib" "%STOUTWALL_OBJ%\library.lib" /Fe"%OUT%" /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 d2d1.lib windowscodecs.lib shell32.lib ole32.lib user32.lib gdi32.lib advapi32.lib uxtheme.lib winhttp.lib
if errorlevel 1 exit /b 3
"%OUT%" "%STOUTWALL_OUT%" "%ROOT%\build\tests\isolated-current-20261121-01\Gallery" "%ROOT%\build\tests\preview-%LABEL%-%RANDOM%"
exit /b %ERRORLEVEL%
