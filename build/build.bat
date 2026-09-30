@echo off
rem ============================================================
rem  RackDraw native one-key build (MinGW-w64 / MSYS2)
rem
rem  Output:
rem    dist\RackDraw.exe       32-bit (runs on 32- and 64-bit Windows)
rem    dist\RackDraw-x64.exe   64-bit (only when a 64-bit toolchain exists)
rem
rem  Toolchain locations can be overridden by environment variables
rem  MINGW32_BIN / MINGW64_BIN (default: C:\msys64\mingw32, C:\msys64\mingw64).
rem
rem  IMPORTANT: this file must stay pure ASCII. cmd.exe reads a .bat in
rem  the console code page; non-ASCII bytes corrupt the line parsing.
rem  Paths are kept relative: MinGW tools on this machine fail on
rem  absolute paths that contain non-ASCII characters.
rem ============================================================
setlocal
pushd "%~dp0.."

if not defined MINGW32_BIN set "MINGW32_BIN=C:\msys64\mingw32\bin"
if not defined MINGW64_BIN set "MINGW64_BIN=C:\msys64\mingw64\bin"

set "CXX32=%MINGW32_BIN%\i686-w64-mingw32-g++.exe"
set "RC32=%MINGW32_BIN%\windres.exe"
set "CXX64=%MINGW64_BIN%\x86_64-w64-mingw32-g++.exe"
set "RC64=%MINGW64_BIN%\windres.exe"

set "HAVE32=0"
set "HAVE64=0"
if exist "%CXX32%" if exist "%RC32%" set "HAVE32=1"
if exist "%CXX64%" if exist "%RC64%" set "HAVE64=1"

if "%HAVE32%%HAVE64%"=="00" (
  echo [ERROR] No MinGW-w64 toolchain found.
  echo         Looked in "%MINGW32_BIN%" and "%MINGW64_BIN%".
  echo         Install the 32-bit toolchain: pacman -S mingw-w64-i686-gcc
  echo         Install the 64-bit toolchain: pacman -S mingw-w64-x86_64-gcc
  popd
  exit /b 1
)

if not exist dist mkdir dist
if not exist build\obj mkdir build\obj

rem Build date is compiled into the program
set "BUILD_DATE="
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set "BUILD_DATE=%%i"
if "%BUILD_DATE%"=="" set "BUILD_DATE=unknown"

if "%HAVE32%"=="1" (
  set "PATH=%MINGW32_BIN%;%PATH%"
  call :BuildTarget x86 "%CXX32%" "%RC32%" "RackDraw.exe"
  if errorlevel 1 goto :failed
) else (
  echo [WARN] 32-bit toolchain not found - the deliverable will be 64-bit only
  echo        and will NOT run on 32-bit Windows.
)

if "%HAVE64%"=="1" (
  set "PATH=%MINGW64_BIN%;%PATH%"
  if "%HAVE32%"=="1" (
    call :BuildTarget x64 "%CXX64%" "%RC64%" "RackDraw-x64.exe"
  ) else (
    call :BuildTarget x64 "%CXX64%" "%RC64%" "RackDraw.exe"
  )
  if errorlevel 1 goto :failed
)

echo.
echo Done. Output in dist\ :
for %%f in ("dist\RackDraw.exe" "dist\RackDraw-x64.exe") do (
  if exist %%f echo   %%~nxf   %%~zf bytes
)
echo Tip: run "dist\RackDraw.exe --selftest" to check the core logic.
popd
exit /b 0

rem ------------------------------------------------------------
rem  %1 = tag (x86/x64), %2 = g++, %3 = windres, %4 = output exe
rem ------------------------------------------------------------
:BuildTarget
set "TAG=%~1"
set "GXX=%~2"
set "RC=%~3"
set "OUTEXE=%~4"

echo [%TAG%] compiling resources ...
"%RC%" --codepage=65001 -Isrc -Isrc\resource -i src\resource\app.rc -o "build\obj\app_res_%TAG%.o"
if errorlevel 1 exit /b 1

echo [%TAG%] compiling and linking ...
"%GXX%" -std=c++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 ^
  -finput-charset=UTF-8 -fexec-charset=UTF-8 -DAPP_BUILD_DATE=\"%BUILD_DATE%\" ^
  -Wall -Wextra -Isrc -municode -mwindows -static -s ^
  -Wl,--major-subsystem-version=6 -Wl,--minor-subsystem-version=1 ^
  -o "dist\%OUTEXE%" ^
  src\main.cpp ^
  src\app\single_instance.cpp src\app\dpi_aware.cpp ^
  src\core\catalog.cpp src\core\geometry.cpp src\core\markdown.cpp ^
  src\core\store.cpp src\core\selftest.cpp ^
  src\ui\theme.cpp src\ui\rack_view.cpp src\ui\image_export.cpp ^
  src\ui\dialogs.cpp src\ui\main_window.cpp ^
  src\util\text_convert.cpp src\util\shell_open.cpp ^
  "build\obj\app_res_%TAG%.o" ^
  -lgdiplus -lwindowscodecs -lole32 -loleaut32 -luuid -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32
exit /b %errorlevel%

:failed
echo.
echo BUILD FAILED
popd
exit /b 1
