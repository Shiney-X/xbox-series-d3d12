add_executable(xbox_phase0_probe
    phase0_probe.cpp)

target_compile_definitions(xbox_phase0_probe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00)

if(MSVC)
    target_compile_options(xbox_phase0_probe PRIVATE /W4 /permissive- /EHsc)
endif()

if(BUILD_TESTING)
    add_test(NAME phase0.capabilities COMMAND xbox_phase0_probe --capabilities)
    add_test(NAME phase0.virtual-memory COMMAND xbox_phase0_probe --memory)
    add_test(NAME phase0.executable-memory COMMAND xbox_phase0_probe --execution)
endif()
