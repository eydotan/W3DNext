@echo off
REM Build the w3d_parity_diff oracle in the pinned VS2022 x86 toolchain.
REM Mirrors scripts/dev_build.bat's env discipline (clear stray RC, pin VS2022,
REM vcpkg toolchain via the win32-vcpkg-debug preset).
setlocal
set "RC="
set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "REPO=%~dp0..\.."
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || (echo [FATAL] vcvarsall failed & exit /b 1)
set "VCPKG_ROOT=%VS%\VC\vcpkg"
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%REPO%"

if not exist "build\win32-vcpkg-debug\CMakeCache.txt" (
    echo === configure ^(fresh^) ===
    cmake --preset win32-vcpkg-debug || (echo [FATAL] configure failed & exit /b 1)
)
echo === build w3d_parity_diff ===
cmake --build build\win32-vcpkg-debug --config Debug --target w3d_parity_diff || (echo [FATAL] build failed & exit /b 1)
echo [DONE] w3d_parity_diff built
exit /b 0
