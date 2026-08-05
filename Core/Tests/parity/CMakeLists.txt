# w3d_parity_diff - PNG frame-diff oracle for the DX8-vs-D3D11 parity pass
# (RENDERER_PORT.md step 10). A standalone development oracle, not a shipped
# game target. It links NO WWVegas library: it only needs zlib to inflate the
# IDAT stream that W3DDisplay::takeScreenShot already produces.
#
# zlib comes from vcpkg under the win32-vcpkg-* presets (ZLIB::ZLIB); the tree's
# bundled github zlib (cmake/zlib.cmake, Z_PREFIX'd) is the fallback the game
# uses when find_package fails. We deliberately prefer the un-prefixed vcpkg
# zlib so this tool never inherits the `#define Byte z_Byte` collision.

add_executable(w3d_parity_diff
    w3d_parity_diff.cpp
)

find_package(ZLIB)
if(ZLIB_FOUND)
    target_link_libraries(w3d_parity_diff PRIVATE ZLIB::ZLIB)
else()
    # Compression's CMakeLists may already have pulled in the same fallback
    # (non-vcpkg presets) - creating libzlib twice is a hard configure error.
    if(NOT TARGET libzlib)
        include(${CMAKE_SOURCE_DIR}/cmake/zlib.cmake)
    endif()
    target_link_libraries(w3d_parity_diff PRIVATE libzlib)
endif()

if(MSVC)
    # Standalone console oracle; no engine warning policy here.
    target_compile_definitions(w3d_parity_diff PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
