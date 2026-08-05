# Standalone test executables. These are development oracles, not shipped
# game targets.

if(WIN32)
    add_subdirectory(d3d11_smoke)
    add_subdirectory(parity)
endif()
