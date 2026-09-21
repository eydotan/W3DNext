# Standalone test executables. These are development oracles, not shipped
# game targets.

if(WIN32)
    # The smoke oracle compiles the D3D11 backend directly, so it follows the
    # same rule as the backend itself: not on the vc6 toolchain, which has
    # neither <unordered_map> nor a D3D11 SDK.
    if(NOT IS_VS6_BUILD)
        add_subdirectory(d3d11_smoke)
    endif()
    add_subdirectory(parity)
endif()
