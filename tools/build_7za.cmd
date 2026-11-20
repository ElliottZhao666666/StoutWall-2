@echo off
setlocal EnableExtensions
rem Run after initializing the x86 MSVC environment in build.cmd.
set "SRC=%~dp0..\build\third-party\7zip-25.01"
set "PIN=5e96a8279489832924056b1fa82f29d5837c9469"
set "MY_DYNAMIC_LINK="
set "MY_SINGLE_THREAD="
if not exist "%SRC%\.git" (
    git clone --quiet --depth 1 --branch 25.01 https://github.com/ip7z/7zip.git "%SRC%"
    if errorlevel 1 exit /b 1
)
set "ACTUAL="
for /f %%H in ('git -C "%SRC%" rev-parse --verify HEAD') do set "ACTUAL=%%H"
if /i not "%ACTUAL%"=="%PIN%" (echo Unexpected 7-Zip source revision.& exit /b 2)
git -C "%SRC%" diff --quiet HEAD --
if errorlevel 1 (echo The pinned 7-Zip source has local changes.& exit /b 2)
pushd "%SRC%\CPP\7zip\Bundles\Alone"
nmake /nologo PLATFORM=x86 "LFLAGS=/Brepro /SUBSYSTEM:CONSOLE,6.01" > "%SRC%\build-7za.log" 2>&1
if errorlevel 1 (popd& type "%SRC%\build-7za.log"& exit /b 3)
popd
copy /Y "%SRC%\CPP\7zip\Bundles\Alone\x86\7za.exe" "%~1\7za.exe" >nul
if errorlevel 1 exit /b 4
for %%F in (License.txt copying.txt unRarLicense.txt) do (
    copy /Y "%SRC%\DOC\%%F" "%~1\7zip-%%F" >nul
    if errorlevel 1 exit /b 4
)
exit /b 0
