# SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set(XBOX_MESA_ROOT "$ENV{XBOX_MESA_ROOT}" CACHE PATH "Pinned Kyty-patched Mesa translator SDK")
if(XBOX_MESA_ROOT)
    foreach(file include/spirv_to_dxil.h include/dxil_versions.h lib/spirv_to_dxil.lib bin/spirv_to_dxil.dll provenance.json)
        if(NOT EXISTS "${XBOX_MESA_ROOT}/${file}")
            message(FATAL_ERROR "Missing Mesa SDK file: ${XBOX_MESA_ROOT}/${file}")
        endif()
    endforeach()
    file(READ "${XBOX_MESA_ROOT}/provenance.json" mesa_provenance)
    # PowerShell 5 can write a BOM; strip it before CMake's JSON parser.
    string(FIND "${mesa_provenance}" "{" mesa_json_start)
    if(mesa_json_start LESS 0)
        message(FATAL_ERROR "Malformed Mesa SDK provenance JSON")
    endif()
    string(SUBSTRING "${mesa_provenance}" ${mesa_json_start} -1 mesa_provenance)
    string(JSON mesa_commit GET "${mesa_provenance}" mesa_commit)
    string(JSON mesa_patch GET "${mesa_provenance}" patch_sha256)
    if(NOT mesa_commit STREQUAL "e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae" OR
       NOT mesa_patch STREQUAL "326b1367c84959a363feeec28c4d1c3adcc6b935ff891dfde1ce1d87b44b29e1")
        message(FATAL_ERROR "Mesa SDK ABI/revision does not match the audited Kyty patch")
    endif()
    add_library(xbox_mesa_translator SHARED IMPORTED GLOBAL)
    set_target_properties(xbox_mesa_translator PROPERTIES
        IMPORTED_IMPLIB "${XBOX_MESA_ROOT}/lib/spirv_to_dxil.lib"
        IMPORTED_LOCATION "${XBOX_MESA_ROOT}/bin/spirv_to_dxil.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${XBOX_MESA_ROOT}/include")
endif()
