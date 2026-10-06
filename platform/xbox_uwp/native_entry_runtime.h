// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/native_entry_prefix.h"
#include "probes.h"
#include <Windows.h>

struct NativeEntryResult {
  bool plan_verified{}, entered{}, stopped_at_import{}, capture_verified{},
      permissions_verified{}, returned_to_host{}, worker_joined{}, cleanup{};
  DWORD error{};
  bool title_input{};
  std::uint64_t entry{}, plt{}, slot{}, mapped_bias{}, relative_applied{};
  Core::Uwp::NativeEntryCapture capture;
  std::string blocker = "not_started", import_key, import_name;
  [[nodiscard]] bool DiagnosticPassed() const;
  [[nodiscard]] std::string Details() const;
};

// Executes ONLY the certified path to its first import. No general guest call,
// automatic success stub, exception API bypass, loop or arbitrary timeout kill.
// Every input is revalidated against the same immutable snapshot before RX.
NativeEntryResult RunNativeEntryPrefix(std::span<const std::uint8_t> snapshot,
                                       void *mapped, std::size_t mapped_bytes,
                                       Core::EntryParams *params,
                                       bool title_input);
XboxSeriesD3D12::Phase0::ProbeResult ProbeNativeEntry();
bool NativeEntryGatePassed() noexcept;
