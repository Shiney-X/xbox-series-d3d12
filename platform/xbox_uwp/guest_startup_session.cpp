// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_startup_session.h"
#include "core/uwp/guest_link_fixture.h"
#include <sstream>

namespace {
bool ProtectData(void *base, std::size_t bytes, DWORD protection) {
  DWORD old{};
  return VirtualProtectFromApp(base, bytes, protection, &old) != FALSE;
}
bool VerifyProtection(const void *address, DWORD protection,
                      DWORD state = MEM_COMMIT) {
  MEMORY_BASIC_INFORMATION info{};
  return VirtualQuery(address, &info, sizeof(info)) && info.State == state &&
         (state == MEM_RESERVE || info.Protect == protection);
}
} // namespace

GuestStartupSession::~GuestStartupSession() { (void)Release(); }
bool GuestStartupSession::Release() noexcept {
  bool released = true;
  for (void **allocation : {&tls_, &stack_, &image_}) {
    if (*allocation) {
      if (VirtualFree(*allocation, 0, MEM_RELEASE))
        *allocation = nullptr;
      else
        released =
            false; // Preserve pointer to live backing; never lose ownership.
    }
  }
  cleanup_verified_ = released;
  prepared_ = false;
  return released;
}
bool GuestStartupSession::Prepare(std::span<const std::uint8_t> snapshot) {
  using namespace Core::Uwp;
  if (!Release()) {
    blocker_ = "cleanup_failed";
    return false;
  }
  // Reset diagnostics on repeated attempts as well as resetting allocations.
  mapped_verified_ = startup_verified_ = protections_verified_ = false;
  error_ = ERROR_SUCCESS;
  image_bytes_ = load_bias_ = relatives_ = unresolved_ = pending_relocations_ =
      0;
  tls_memory_ = tls_file_ = entry_ = 0;
  unresolved_keys_.clear();
  native_entry_called_ = false;
  const auto fail = [&](const char *blocker, DWORD error) {
    blocker_ = blocker;
    error_ = error;
    (void)Release();
    return false;
  };
  try {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (system.dwPageSize != 4096)
      return fail("host_page_size_unsupported", ERROR_NOT_SUPPORTED);
    const auto payload = StageGuestPayload(snapshot);
    const auto manifest = InspectGuestLinkManifest(snapshot, true);
    if (!payload.Ready() || !manifest.valid)
      return fail("payload_or_manifest_invalid", ERROR_BAD_EXE_FORMAT);
    const auto flags = StartupPageFlags(payload.image.plan);
    if (flags.empty())
      return fail("page_permissions_unsupported", ERROR_NOT_SUPPORTED);
    image_bytes_ = payload.image.bytes.size();
    tls_memory_ = manifest.tls_memory_bytes;
    tls_file_ = manifest.tls_file_bytes;
    if (manifest.tls_alignment > 32 || tls_memory_ > 16 * 1024 * 1024)
      return fail("main_tls_alignment_or_budget_unsupported",
                  ERROR_NOT_SUPPORTED);
    constexpr std::size_t page = 4096;
    image_ = VirtualAllocFromApp(
        nullptr, static_cast<SIZE_T>(image_bytes_) + 2 * GuestPageSize,
        MEM_RESERVE, PAGE_NOACCESS);
    if (!image_)
      return fail("image_reservation_failed", GetLastError());
    auto *mapped = static_cast<std::uint8_t *>(image_) + GuestPageSize;
    if (reinterpret_cast<std::uint64_t>(mapped) % GuestPageSize)
      return fail("guest_image_alignment_failed", ERROR_INVALID_ADDRESS);
    if (VirtualAllocFromApp(mapped, static_cast<SIZE_T>(image_bytes_),
                            MEM_COMMIT, PAGE_READWRITE) != mapped ||
        reinterpret_cast<std::uint64_t>(mapped) < payload.image.plan.base)
      return fail("image_commit_or_bias_failed", ERROR_NOT_ENOUGH_MEMORY);
    load_bias_ =
        reinterpret_cast<std::uint64_t>(mapped) - payload.image.plan.base;
    // Registry intentionally empty until callable exports/general faults are
    // integrated. Apply validated relatives with the ACTUAL allocation bias.
    const auto linked = StageGuestDataLink(snapshot, load_bias_);
    if (!linked.valid || !linked.writes_verified || !linked.untouched_verified)
      return fail("actual_bias_link_failed", ERROR_BAD_EXE_FORMAT);
    relatives_ = linked.relative_applied;
    unresolved_ = linked.imports.unresolved;
    pending_relocations_ =
        linked.pending_imports + linked.pending_bindings + linked.pending_types;
    unsigned reported = 0;
    for (const auto &binding : linked.imports.bindings) {
      if (!binding.numeric_address && reported++ < 8) {
        if (!unresolved_keys_.empty())
          unresolved_keys_ += ',';
        unresolved_keys_ += binding.key.Label();
      }
    }
    std::memcpy(mapped, linked.payload.image.bytes.data(),
                static_cast<std::size_t>(image_bytes_));
    mapped_verified_ = std::memcmp(mapped, linked.payload.image.bytes.data(),
                                   static_cast<std::size_t>(image_bytes_)) == 0;
    if (!mapped_verified_)
      return fail("mapped_readback_failed", ERROR_INVALID_DATA);
    // Stack reservation with two inaccessible guards; no host stack switching.
    stack_ = VirtualAllocFromApp(nullptr, GuestStartupStackSize + 2 * page,
                                 MEM_RESERVE, PAGE_NOACCESS);
    auto *stack = stack_ ? static_cast<std::uint8_t *>(stack_) + page : nullptr;
    if (!stack || VirtualAllocFromApp(stack, GuestStartupStackSize, MEM_COMMIT,
                                      PAGE_READWRITE) != stack)
      return fail("startup_stack_allocation_failed", ERROR_NOT_ENOUGH_MEMORY);
    const auto tls_size =
        (static_cast<std::size_t>(tls_memory_) + 31) / 32 * 32 + 88;
    tls_ = VirtualAllocFromApp(nullptr, tls_size, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
    if (!tls_)
      return fail("startup_tls_allocation_failed", GetLastError());
    std::span<const std::uint8_t> initial;
    if (tls_file_) {
      if (manifest.tls_address < payload.image.plan.base ||
          manifest.tls_address - payload.image.plan.base > image_bytes_ ||
          tls_file_ >
              image_bytes_ - (manifest.tls_address - payload.image.plan.base))
        return fail("main_tls_source_outside_image", ERROR_BAD_EXE_FORMAT);
      initial = {mapped + manifest.tls_address - payload.image.plan.base,
                 static_cast<std::size_t>(tls_file_)};
    }
    entry_ = reinterpret_cast<std::uint64_t>(mapped) +
             payload.image.plan.entry_offset;
    const auto startup = PrepareGuestStartup(
        {stack, GuestStartupStackSize},
        {static_cast<std::uint8_t *>(tls_), tls_size}, initial, tls_memory_,
        std::max<std::uint64_t>(1, manifest.tls_alignment), entry_,
        "/app0/eboot.bin");
    if (!startup.valid)
      return fail("startup_layout_failed", ERROR_INVALID_DATA);
    startup_ = startup;
    Core::EntryParams params{};
    Core::Tcb tcb{};
    std::array<Core::DtvEntry, 3> dtv{};
    std::memcpy(&params, stack + 0x100, sizeof(params));
    std::memcpy(&tcb, reinterpret_cast<const void *>(startup.tcb), sizeof(tcb));
    std::memcpy(dtv.data(), reinterpret_cast<const void *>(startup.dtv),
                sizeof(dtv));
    startup_verified_ =
        params.argc == 1 && params.padding == 0 &&
        params.entry_addr == entry_ && params.argv[1] == nullptr &&
        params.argv[0] == reinterpret_cast<const char *>(stack + 0x400) &&
        std::strcmp(params.argv[0], "/app0/eboot.bin") == 0 &&
        startup.initial_rsp % 16 == 8 &&
        std::memcmp(reinterpret_cast<const void *>(startup.initial_rsp),
                    &params, 16) == 0 &&
        reinterpret_cast<std::uint64_t>(tcb.tcb_self) == startup.tcb &&
        reinterpret_cast<std::uint64_t>(tcb.tcb_dtv) == startup.dtv &&
        !tcb.tcb_thread && dtv[0].counter == 1 && dtv[1].counter == 1 &&
        reinterpret_cast<std::uint64_t>(dtv[2].pointer) ==
            (tls_memory_ ? startup.tls : 0);
    startup_verified_ =
        startup_verified_ &&
        (initial.empty() ||
         std::equal(initial.begin(), initial.end(),
                    static_cast<const std::uint8_t *>(tls_))) &&
        std::all_of(static_cast<const std::uint8_t *>(tls_) + tls_file_,
                    static_cast<const std::uint8_t *>(tls_) + tls_memory_,
                    [](auto byte) { return byte == 0; });
    if (!startup_verified_)
      return fail("startup_readback_failed", ERROR_INVALID_DATA);
    for (std::size_t i = 0; i < flags.size(); ++i) {
      const DWORD protection = !flags[i]      ? PAGE_NOACCESS
                               : flags[i] & 2 ? PAGE_READWRITE
                                              : PAGE_READONLY;
      if (!ProtectData(mapped + i * page, page, protection) ||
          !VerifyProtection(mapped + i * page, protection))
        return fail("data_only_protection_failed", ERROR_INVALID_DATA);
    }
    protections_verified_ =
        VerifyProtection(image_, 0, MEM_RESERVE) &&
        VerifyProtection(mapped + image_bytes_, 0, MEM_RESERVE) &&
        VerifyProtection(stack_, 0, MEM_RESERVE) &&
        VerifyProtection(stack + GuestStartupStackSize, 0, MEM_RESERVE);
    if (!protections_verified_)
      return fail("guard_protection_failed", ERROR_INVALID_DATA);
    prepared_ = true;
    cleanup_verified_ =
        false; // Backing is intentionally owned until explicit Release.
    blocker_ = unresolved_            ? "unresolved_imports"
               : pending_relocations_ ? "pending_relocations"
                                      : "general_entry_boundary_not_integrated";
    return true; // Preparation succeeded; entry remains BLOCKED, never called.
  } catch (...) {
    return fail("startup_exception", ERROR_GEN_FAILURE);
  }
}
NativeEntryResult
GuestStartupSession::AttemptNativeEntry(std::span<const std::uint8_t> snapshot,
                                        bool authored_gate) {
  if (!prepared_ || !startup_.valid ||
      (!authored_gate && !NativeEntryGatePassed())) {
    NativeEntryResult result;
    result.blocker = !prepared_ ? "startup_not_prepared"
                                : "native_entry_fixture_gate_failed";
    result.error = ERROR_NOT_SUPPORTED;
    return result;
  }
  const auto result = RunNativeEntryPrefix(
      snapshot, static_cast<std::uint8_t *>(image_) + Core::Uwp::GuestPageSize,
      static_cast<std::size_t>(image_bytes_),
      reinterpret_cast<Core::EntryParams *>(startup_.params), !authored_gate);
  native_entry_called_ =
      native_entry_called_ || (result.entered && !authored_gate);
  if (result.error != ERROR_SUCCESS) {
    prepared_ = false;
    protections_verified_ = false;
  }
  return result;
}
std::string GuestStartupSession::Details() const {
  std::ostringstream out;
  out << "stage=startup_preparation;scope=main_module_data_only;preparation_"
         "passed="
      << prepared_ << ";mapped_readback_verified=" << mapped_verified_
      << ";entry_stack_tcb_dtv_prepared=" << startup_verified_
      << ";data_only_protections_verified=" << protections_verified_
      << ";image_bytes=" << image_bytes_ << ";load_bias=" << load_bias_
      << ";address_scope=actual_owned_mapping;relative_applied=" << relatives_
      << ";unresolved_imports=" << unresolved_
      << ";pending_relocations=" << pending_relocations_
      << ";tls_memory_bytes=" << tls_memory_ << ";tls_file_bytes=" << tls_file_
      << ";stack_bytes=" << Core::Uwp::GuestStartupStackSize
      << ";entry_address=" << entry_ << ";blocker=" << blocker_
      << ";first_unresolved_keys=" << unresolved_keys_
      << ";image_retained=" << (image_ != nullptr)
      << ";cleanup_verified=" << cleanup_verified_ << ";win32_error=" << error_
      << ";upstream_startup_layout_shared=1;upstream_linker_execute_integrated="
         "0"
         ";tls_main_module_only=1;tls_bound=0;entry_boundary_ready=0;ready_for_"
         "entry=0"
         ";game_code_executable=0;guest_entry_called="
      << native_entry_called_ << ";game_executed=" << native_entry_called_
      << ";game_boot_completed=0;game_frame=0";
  return out.str();
}
XboxSeriesD3D12::Phase0::ProbeResult ProbeGuestStartup() {
  GuestStartupSession session;
  bool passed = true;
  std::string details;
  for (const bool self : {false, true}) {
    const auto file = Core::Uwp::MakeGuestLinkFixture(self);
    const bool prepared =
        session.Prepare(file) && session.Blocker() == "unresolved_imports";
    details = session.Details();
    passed = session.Release() && prepared && passed;
  }
  auto invalid = Core::Uwp::MakeGuestLinkFixture(false);
  invalid[68] = 7; // W+X LOAD must fail before a runnable mapping exists.
  passed = !session.Prepare(invalid) && session.Release() && passed;
  return {"guest-startup-session", passed,
          passed ? DWORD{ERROR_SUCCESS} : DWORD{ERROR_INVALID_DATA},
          details + ";probe_raw_self_verified=" + std::to_string(passed) +
              ";probe_cleanup_verified=" + std::to_string(passed)};
}
