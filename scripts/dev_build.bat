@echo off
setlocal
REM CMake reads a stray RC env var as the resource-compiler path - keep it clear.
set "RC="
set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "REPO=%~dp0.."
set "LOG=%REPO%\build\dev_build.log"

if not exist "%REPO%\build" mkdir "%REPO%\build"

REM --- vcvarsall's instance discovery needs vswhere on PATH ---
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"

REM --- pin the entire toolchain to VS2022 (14.44) ---
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || (echo [FATAL] vcvarsall failed & exit /b 1)
set "VCPKG_ROOT=%VS%\VC\vcpkg"
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%REPO%"
set "BUILD_RC=0"

(
echo === compiler in use ===
where cl
echo === cmake/ninja ===
where cmake
where ninja

echo === configure ^(fresh^) ===
cmake --preset win32-vcpkg-debug
if errorlevel 1 (
    echo [FATAL] configure failed
) else (
    echo === build z_generals ^(Debug^) ===
    cmake --build build\win32-vcpkg-debug --config Debug --target z_generals
    if errorlevel 1 (
        echo [FATAL] build failed
    ) else (
        echo === verify compiler baked into cache ===
        findstr /C:"CMAKE_CXX_COMPILER:" build\win32-vcpkg-debug\CMakeCache.txt
        echo [DONE] dev_build complete
    )
)
) > "%LOG%" 2>&1

findstr /C:"[FATAL]" "%LOG%" >nul
if not errorlevel 1 set "BUILD_RC=1"

type "%LOG%"
exit /b %BUILD_RC%
