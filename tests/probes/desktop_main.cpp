// SPDX-License-Identifier: GPL-2.0-or-later

#include "probes.h"

#include <iostream>
#include <string_view>
#include <vector>

using XboxSeriesD3D12::Phase0::ProbeResult;

int main(int argc, char** argv) {
    const std::string_view command = argc > 1 ? argv[1] : "--all";
    std::vector<ProbeResult> results;

    if (command == "--all") {
        results = XboxSeriesD3D12::Phase0::RunAllProbes();
    } else if (command == "--capabilities") {
        results.push_back(XboxSeriesD3D12::Phase0::ProbeCapabilities());
    } else if (command == "--memory") {
        results.push_back(XboxSeriesD3D12::Phase0::ProbeVirtualMemory());
    } else if (command == "--memory-aliases") {
        results.push_back(XboxSeriesD3D12::Phase0::ProbeMemoryAliases());
    } else if (command == "--execution") {
        results.push_back(XboxSeriesD3D12::Phase0::ProbeExecutableMemory());
    } else if (command == "--d3d12") {
        results.push_back(XboxSeriesD3D12::Phase0::ProbeD3D12Device());
    } else {
        std::cerr << "usage: xbox_phase0_probe "
                     "[--all|--capabilities|--memory|--memory-aliases|--execution|--d3d12]\n";
        return 2;
    }

    std::cout << XboxSeriesD3D12::Phase0::SerializeJsonLines(results);
    return XboxSeriesD3D12::Phase0::AllPassed(results) ? 0 : 1;
}
