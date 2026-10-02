// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "d3d12_videocore_bridge.h"

struct D3D12VideoCoreProbeResult {
  bool buffer_verified{};
  bool texture_verified{};
  UINT64 dma_ticket{};
  UINT64 frame_ticket{};
  D3D12VideoCoreStats stats;
  D3D12Resource frame;
  [[nodiscard]] bool Passed() const noexcept {
    return buffer_verified && texture_verified && frame.Get() &&
           stats.rejected_requests == 0 && stats.unsupported_requests == 0;
  }
};

// Uses the real GpuCommandSink polymorphically, not PM4 packets. Creates a
// GPU-generated green/orange BGRA frame for the host's Diagnostics preview.
[[nodiscard]] D3D12VideoCoreProbeResult
RunD3D12VideoCoreProbe(D3D12DeviceContext &context,
                       D3D12ResourceAllocator &allocator,
                       D3D12TransferStats &transfers);
