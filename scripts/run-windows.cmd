@echo off
setlocal EnableExtensions
REM Build (if needed) and launch WALTOSH with Qt MinGW DLLs on PATH.

set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1

if defined QT_ROOT (
  set "QTROOT=%QT_ROOT%"
) else if exist "C:\Qt\6.9.2\mingw_64" (
  set "QTROOT=C:\Qt\6.9.2\mingw_64"
) else (
  echo Set QT_ROOT to your Qt mingw_64 folder, e.g. C:\Qt\6.9.2\mingw_64
  popd
  exit /b 1
)

set "MINGW_BIN=%QTROOT%\..\..\Tools\mingw1310_64\bin"
if not exist "%MINGW_BIN%\g++.exe" set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
if not exist "%MINGW_BIN%\g++.exe" (
  echo MinGW not found. Expected Tools\mingw1310_64 next to Qt, or C:\Qt\Tools\mingw1310_64
  popd
  exit /b 1
)

set "PATH=%MINGW_BIN%;%QTROOT%\bin;%PATH%"
set "BUILD_DIR=%ROOT%\build"
set "EXE=%BUILD_DIR%\Waltosh.exe"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake not found on PATH.
  popd
  exit /b 1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
  echo Configuring CMake...
  cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTROOT%"
  if errorlevel 1 (
    echo CMake configure failed.
    popd
    exit /b 1
  )
)

echo Building Waltosh...
cmake --build "%BUILD_DIR%" --target Waltosh -j
if errorlevel 1 (
  echo Build failed. If Waltosh.exe is running, close it and retry.
  popd
  exit /b 1
)

if not exist "%EXE%" (
  echo Build finished but Waltosh.exe was not found at:
  echo   %EXE%
  popd
  exit /b 1
)

pushd "%BUILD_DIR%"
"%EXE%"
set "CODE=%ERRORLEVEL%"
popd
popd

if not "%CODE%"=="0" (
  echo WALTOSH exited with code %CODE%.
  exit /b %CODE%
)
