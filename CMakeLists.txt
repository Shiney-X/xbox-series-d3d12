cmake_minimum_required(VERSION 3.25)

project(xbox_series_d3d12
    VERSION 0.1.0
    DESCRIPTION "Feasibility work for a shadPS4 Xbox Series D3D12 port"
    LANGUAGES CXX)

option(XBOX_D3D12_BUILD_PROBES "Build Phase 0 Windows feasibility probes" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

if(XBOX_D3D12_BUILD_PROBES)
    if(WIN32)
        add_subdirectory(tests/probes)
    else()
        message(STATUS "Phase 0 probes require the Windows SDK; targets were skipped")
    endif()
endif()
