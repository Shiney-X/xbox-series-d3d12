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
    add_subdirectory(../guest_preflight guest_preflight)
    add_executable(xbox_guest_execution_test
        ../../src/core/aerolib/aerolib.cpp
        ../../platform/xbox_uwp/native_entry_runtime.cpp
        ../../platform/xbox_uwp/guest_startup_session.cpp
        guest_execution_test.cpp ../../platform/xbox_uwp/guest_execution_probe.cpp)
    target_include_directories(xbox_guest_execution_test PRIVATE
        ../../platform/xbox_uwp ../../src ../../src/phase0)
    target_compile_features(xbox_guest_execution_test PRIVATE cxx_std_20)
    target_compile_definitions(xbox_guest_execution_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_guest_execution_test PRIVATE WindowsApp)
    if(MSVC)
        target_compile_options(xbox_guest_execution_test PRIVATE /W4 /WX /permissive- /EHsc /bigobj)
    endif()
    add_test(NAME phase5.guest-execution COMMAND xbox_guest_execution_test)
    set_tests_properties(phase5.guest-execution PROPERTIES TIMEOUT 30)
    add_executable(xbox_native_entry_test native_entry_test.cpp
        ../../platform/xbox_uwp/native_entry_runtime.cpp
        ../../platform/xbox_uwp/guest_startup_session.cpp
        ../../src/core/aerolib/aerolib.cpp)
    target_include_directories(xbox_native_entry_test PRIVATE
        ../../platform/xbox_uwp ../../src ../../src/phase0)
    target_compile_features(xbox_native_entry_test PRIVATE cxx_std_20)
    target_compile_definitions(xbox_native_entry_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_native_entry_test PRIVATE WindowsApp)
    if(MSVC)
        target_compile_options(xbox_native_entry_test PRIVATE /W4 /WX /permissive- /EHsc /bigobj)
    endif()
    add_test(NAME phase5.native-entry-boundary COMMAND xbox_native_entry_test)
    set_tests_properties(phase5.native-entry-boundary PROPERTIES TIMEOUT 30)
    add_subdirectory(shaders)
    add_executable(xbox_d3d12_videocore_test
        d3d12_videocore_test.cpp
        ../../platform/xbox_uwp/d3d12_device_context.cpp
        ../../platform/xbox_uwp/d3d12_resource_allocator.cpp
        ../../platform/xbox_uwp/d3d12_pipeline_cache.cpp
        ../../platform/xbox_uwp/d3d12_command_encoder.cpp
        ../../platform/xbox_uwp/d3d12_videocore_bridge.cpp
        ../../platform/xbox_uwp/d3d12_videocore_probe.cpp)
    target_include_directories(xbox_d3d12_videocore_test PRIVATE
        ../../platform/xbox_uwp ../../src)
    target_compile_definitions(xbox_d3d12_videocore_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_d3d12_videocore_test PRIVATE d3d12 d3dcompiler dxgi WindowsApp)
    if(MSVC)
        target_compile_options(xbox_d3d12_videocore_test PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME phase3.d3d12-videocore COMMAND xbox_d3d12_videocore_test)
    set_tests_properties(phase3.d3d12-videocore PROPERTIES TIMEOUT 60)

    add_executable(xbox_d3d12_transfers_test
        d3d12_transfers_test.cpp
        ../../platform/xbox_uwp/d3d12_device_context.cpp
        ../../platform/xbox_uwp/d3d12_resource_allocator.cpp
        ../../platform/xbox_uwp/d3d12_pipeline_cache.cpp
        ../../platform/xbox_uwp/d3d12_command_encoder.cpp
        ../../platform/xbox_uwp/d3d12_transfer_probe.cpp)
    target_include_directories(xbox_d3d12_transfers_test PRIVATE ../../platform/xbox_uwp)
    target_compile_definitions(xbox_d3d12_transfers_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_d3d12_transfers_test PRIVATE d3d12 dxgi WindowsApp)
    if(MSVC)
        target_compile_options(xbox_d3d12_transfers_test PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME phase3.d3d12-transfers COMMAND xbox_d3d12_transfers_test)
    set_tests_properties(phase3.d3d12-transfers PROPERTIES TIMEOUT 60)

    add_executable(xbox_d3d12_pipelines_test
        d3d12_pipelines_test.cpp
        ../../platform/xbox_uwp/d3d12_device_context.cpp
        ../../platform/xbox_uwp/d3d12_resource_allocator.cpp
        ../../platform/xbox_uwp/d3d12_pipeline_cache.cpp
        ../../platform/xbox_uwp/d3d12_compute_probe.cpp)
    target_sources(xbox_d3d12_pipelines_test PRIVATE
        ../../platform/xbox_uwp/d3d12_graphics_probe.cpp
        ../../platform/xbox_uwp/d3d12_command_encoder.cpp)
    target_include_directories(xbox_d3d12_pipelines_test PRIVATE ../../platform/xbox_uwp)
    target_compile_definitions(xbox_d3d12_pipelines_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_d3d12_pipelines_test PRIVATE d3d12 d3dcompiler dxcompiler dxgi WindowsApp xbox_spirv_hlsl xbox_upstream_spirv)
    cmake_host_system_information(RESULT shader_sdk_root QUERY WINDOWS_REGISTRY
        "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE "KitsRoot10" VIEW 32_64)
    file(TO_CMAKE_PATH "${shader_sdk_root}" shader_sdk_root)
    foreach(runtime dxcompiler.dll dxil.dll)
        find_file(shader_${runtime} NAMES ${runtime}
            PATHS "${shader_sdk_root}/Redist/D3D/x64" NO_DEFAULT_PATH REQUIRED)
        add_custom_command(TARGET xbox_d3d12_pipelines_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${shader_${runtime}}"
                "$<TARGET_FILE_DIR:xbox_d3d12_pipelines_test>/${runtime}")
    endforeach()
    if(MSVC)
        target_compile_options(xbox_d3d12_pipelines_test PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME phase3.d3d12-pipelines COMMAND xbox_d3d12_pipelines_test)
    set_tests_properties(phase3.d3d12-pipelines PROPERTIES TIMEOUT 60)

    add_executable(xbox_d3d12_submission_test
        d3d12_submission_test.cpp
        ../../platform/xbox_uwp/d3d12_device_context.cpp)
    target_include_directories(xbox_d3d12_submission_test PRIVATE
        ../../platform/xbox_uwp)
    target_compile_definitions(xbox_d3d12_submission_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_d3d12_submission_test PRIVATE d3d12 dxgi WindowsApp)
    if(MSVC)
        target_compile_options(xbox_d3d12_submission_test PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME phase3.d3d12-submission COMMAND xbox_d3d12_submission_test)
    set_tests_properties(phase3.d3d12-submission PROPERTIES TIMEOUT 30)

    add_executable(xbox_d3d12_resources_test
        d3d12_resources_test.cpp
        ../../platform/xbox_uwp/d3d12_device_context.cpp
        ../../platform/xbox_uwp/d3d12_resource_allocator.cpp)
    target_include_directories(xbox_d3d12_resources_test PRIVATE ../../platform/xbox_uwp)
    target_compile_definitions(xbox_d3d12_resources_test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(xbox_d3d12_resources_test PRIVATE d3d12 dxgi WindowsApp)
    if(MSVC)
        target_compile_options(xbox_d3d12_resources_test PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME phase3.d3d12-resources COMMAND xbox_d3d12_resources_test)
    set_tests_properties(phase3.d3d12-resources PROPERTIES TIMEOUT 30)

    add_test(NAME phase0.capabilities COMMAND xbox_phase0_probe --capabilities)
    add_test(NAME phase0.virtual-memory COMMAND xbox_phase0_probe --memory)
    add_test(NAME phase0.memory-aliases COMMAND xbox_phase0_probe --memory-aliases)
    add_test(NAME phase0.executable-memory COMMAND xbox_phase0_probe --execution)
    add_test(NAME phase0.d3d12-device COMMAND xbox_phase0_probe --d3d12)
endif()
