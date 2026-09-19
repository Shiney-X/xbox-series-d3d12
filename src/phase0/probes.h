// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XboxSeriesD3D12::Phase0 {

struct ProbeResult {
    std::string name;
    bool passed{};
    std::uint32_t error{};
    std::string details;
};

[[nodiscard]] std::vector<ProbeResult> RunAllProbes();
[[nodiscard]] ProbeResult ProbeCapabilities();
[[nodiscard]] ProbeResult ProbeVirtualMemory();
[[nodiscard]] ProbeResult ProbeExecutableMemory();
[[nodiscard]] ProbeResult ProbeD3D12Device();
[[nodiscard]] bool AllPassed(const std::vector<ProbeResult>& results) noexcept;
[[nodiscard]] std::string SerializeJsonLine(const ProbeResult& result);
[[nodiscard]] std::string SerializeJsonLines(const std::vector<ProbeResult>& results);

} // namespace XboxSeriesD3D12::Phase0
