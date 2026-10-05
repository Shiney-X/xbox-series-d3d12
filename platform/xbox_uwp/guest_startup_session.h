// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_startup.h"
#include "probes.h"
#include <Windows.h>

// Owned preparation session, deliberately no Execute API. Game code never RX.
class GuestStartupSession {
public:
  GuestStartupSession() = default;
  GuestStartupSession(const GuestStartupSession &) = delete;
  GuestStartupSession &operator=(const GuestStartupSession &) = delete;
  ~GuestStartupSession();
  bool Prepare(std::span<const std::uint8_t> snapshot);
  bool Release() noexcept;
  [[nodiscard]] std::string Details() const;
  [[nodiscard]] const std::string &Blocker() const { return blocker_; }
  [[nodiscard]] bool Prepared() const { return prepared_; }

private:
  void *image_{}, *stack_{}, *tls_{};
  bool prepared_{}, mapped_verified_{}, startup_verified_{},
      protections_verified_{};
  bool cleanup_verified_ = true;
  DWORD error_{};
  std::uint64_t image_bytes_{}, load_bias_{}, relatives_{}, unresolved_{},
      pending_relocations_{};
  std::uint64_t tls_memory_{}, tls_file_{}, entry_{};
  std::string blocker_ = "not_prepared", unresolved_keys_;
};
XboxSeriesD3D12::Phase0::ProbeResult ProbeGuestStartup();
