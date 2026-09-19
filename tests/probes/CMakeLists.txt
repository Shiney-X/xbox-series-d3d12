add_library(xbox_phase0_probe_library STATIC
    ../../src/phase0/probes.cpp
    ../../src/phase0/probes.h)

target_include_directories(xbox_phase0_probe_library PUBLIC
    ../../src/phase0)

target_compile_definitions(xbox_phase0_probe_library PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00)

target_link_libraries(xbox_phase0_probe_library PUBLIC
    d3d12
    dxgi
    WindowsApp)

add_executable(xbox_phase0_probe desktop_main.cpp)

target_link_libraries(xbox_phase0_probe PRIVATE xbox_phase0_probe_library)

target_compile_definitions(xbox_phase0_probe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00)

target_link_libraries(xbox_phase0_probe PRIVATE WindowsApp)

if(MSVC)
    target_compile_options(xbox_phase0_probe_library PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(xbox_phase0_probe PRIVATE /W4 /permissive- /EHsc)
endif()

if(BUILD_TESTING)
    add_test(NAME phase0.capabilities COMMAND xbox_phase0_probe --capabilities)
    add_test(NAME phase0.virtual-memory COMMAND xbox_phase0_probe --memory)
    add_test(NAME phase0.executable-memory COMMAND xbox_phase0_probe --execution)
    add_test(NAME phase0.d3d12-device COMMAND xbox_phase0_probe --d3d12)
endif()
