// SPDX-License-Identifier: GPL-2.0-or-later

#include <winrt/Windows.System.h>
#include <winrt/base.h>

#include "memory_pressure_probe.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>

using namespace winrt::Windows::System;
using XboxSeriesD3D12::Phase0::ProbeResult;

namespace {

constexpr std::uint64_t MiB = 1024ull * 1024ull;
constexpr SIZE_T ChunkSize = 16ull * MiB;
constexpr std::uint64_t MaximumProbeSize = 256ull * MiB;

} // namespace

ProbeResult ProbeAppMemoryPressure() {
  const std::uint64_t limit = MemoryManager::AppMemoryUsageLimit();
  const std::uint64_t expected_limit =
      MemoryManager::ExpectedAppMemoryUsageLimit();
  const std::uint64_t usage_before = MemoryManager::AppMemoryUsage();
  const auto level_before = MemoryManager::AppMemoryUsageLevel();
  if (limit == 0) {
    return {"memory-pressure", false, ERROR_NOT_SUPPORTED,
            "AppMemoryUsageLimit returned zero"};
  }

  // Five percent of the sandbox budget is large enough to make accounting
  // observable without approaching the platform's termination threshold.
  const std::uint64_t headroom =
      limit > usage_before ? limit - usage_before : 0;
  const std::uint64_t requested =
      std::min({MaximumProbeSize, limit / 20, headroom / 4});
  const std::uint64_t target = requested - (requested % ChunkSize);
  if (target < ChunkSize) {
    return {"memory-pressure", false, ERROR_NOT_ENOUGH_MEMORY,
            "sandbox budget is too small for the controlled pressure probe"};
  }

  std::array<void *, MaximumProbeSize / ChunkSize> allocations{};
  std::size_t allocation_count = 0;
  std::uint64_t committed = 0;
  std::uint64_t peak_usage = usage_before;
  DWORD allocation_error = ERROR_SUCCESS;

  while (committed < target) {
    void *const allocation = VirtualAllocFromApp(
        nullptr, ChunkSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (allocation == nullptr) {
      allocation_error = GetLastError();
      break;
    }

    allocations[allocation_count++] = allocation;
    auto *const bytes = static_cast<std::byte *>(allocation);
    for (SIZE_T offset = 0; offset < ChunkSize; offset += 4096) {
      bytes[offset] = static_cast<std::byte>((committed / ChunkSize) + 1);
    }

    committed += ChunkSize;
    peak_usage = std::max(peak_usage, MemoryManager::AppMemoryUsage());
  }

  const auto level_peak = MemoryManager::AppMemoryUsageLevel();
  for (std::size_t index = 0; index < allocation_count; ++index) {
    VirtualFree(allocations[index], 0, MEM_RELEASE);
  }

  const std::uint64_t usage_after = MemoryManager::AppMemoryUsage();
  const auto level_after = MemoryManager::AppMemoryUsageLevel();
  const bool fully_committed = committed == target;
  const bool accounting_observed = peak_usage > usage_before;

  std::ostringstream details;
  details << "limit=" << limit << ";expected_limit=" << expected_limit
          << ";usage_before=" << usage_before << ";target=" << target
          << ";committed=" << committed << ";peak_usage=" << peak_usage
          << ";usage_after=" << usage_after
          << ";level_before=" << static_cast<unsigned>(level_before)
          << ";level_peak=" << static_cast<unsigned>(level_peak)
          << ";level_after=" << static_cast<unsigned>(level_after)
          << ";accounting_observed=" << accounting_observed;

  const DWORD result_error =
      fully_committed
          ? (accounting_observed ? ERROR_SUCCESS : ERROR_INVALID_DATA)
          : allocation_error;
  return {"memory-pressure", fully_committed && accounting_observed,
          result_error, details.str()};
}
