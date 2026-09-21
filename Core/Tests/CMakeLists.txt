# Standalone test executables. These are development oracles, not shipped
# game targets.

# Both oracles serve the D3D11 port and are written in modern C++ - the smoke
# test compiles the backend itself (no <unordered_map>, no D3D11 SDK on VC6) and
# the parity differ needs <cstdint>. Neither has any reason to run on the vc6
# toolchain, which builds the DX8 renderer these tools exist to compare against.
if(WIN32 AND NOT IS_VS6_BUILD)
    add_subdirectory(d3d11_smoke)
    add_subdirectory(parity)
endif()
