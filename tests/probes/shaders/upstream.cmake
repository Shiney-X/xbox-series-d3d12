# SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set(shader_repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(emitter "${shader_repo}/src/shader_recompiler/backend/spirv")
set(ir "${shader_repo}/src/shader_recompiler/ir")
file(GLOB emitter_sources CONFIGURE_DEPENDS "${emitter}/*.cpp")
file(GLOB sirit_sources CONFIGURE_DEPENDS "${shader_repo}/externals/sirit/src/*.cpp"
    "${shader_repo}/externals/sirit/src/instructions/*.cpp")
add_library(xbox_upstream_spirv STATIC ${emitter_sources} ${sirit_sources}
    ${ir}/basic_block.cpp ${ir}/microinstruction.cpp ${ir}/ir_emitter.cpp
    ${ir}/value.cpp ${ir}/type.cpp ${ir}/opcodes.cpp ${ir}/attribute.cpp ${ir}/patch.cpp
    ${shader_repo}/src/video_core/amdgpu/pixel_format.cpp
    ${shader_repo}/platform/xbox_uwp/shaders/upstream_compute.cpp
    ${shader_repo}/platform/xbox_uwp/shaders/compiler_support.cpp)
target_compile_features(xbox_upstream_spirv PUBLIC cxx_std_23)
target_compile_definitions(xbox_upstream_spirv PRIVATE SHAD_SHADER_TOOL_LOGGING SHAD_STANDALONE_SHADER_EMITTER
    FMT_HEADER_ONLY NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(xbox_upstream_spirv PUBLIC ${shader_repo}/platform/xbox_uwp/shaders)
target_include_directories(xbox_upstream_spirv PRIVATE ${shader_repo}/src
    ${shader_repo}/externals/sirit/include ${shader_repo}/externals/sirit/src
    ${shader_repo}/externals/sirit/externals/SPIRV-Headers/include
    ${shader_repo}/externals/ext-boost ${shader_repo}/externals/half/include
    ${shader_repo}/externals/fmt/include ${shader_repo}/externals/magic_enum/include)
if(MSVC)
    target_compile_options(xbox_upstream_spirv PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(xbox_upstream_spirv PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
