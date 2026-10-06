// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_entry_runtime.h"
#include "core/aerolib/aerolib.h"
#include "core/uwp/native_entry_fixture.h"
#include "guest_startup_session.h"
#include <atomic>
#include <exception>
#include <sstream>

namespace {
std::atomic_bool gate_passed{};
struct OwnedCode {
  void *base{};
  ~OwnedCode() {
    if (base)
      VirtualFree(base, 0, MEM_RELEASE);
  }
};
struct NativeWorker {
  Core::EntryParams *params{};
  void *entry{}, *bridge{};
  DWORD thread_id{};
  ULONG_PTR stack_low{}, stack_high{};
  bool returned{};
};
DWORD WINAPI Enter(void *context) {
  auto &worker = *static_cast<NativeWorker *>(context);
  worker.thread_id = GetCurrentThreadId();
  GetCurrentThreadStackLimits(&worker.stack_low, &worker.stack_high);
  using Invoke = void (*)(Core::EntryParams *, void *);
  reinterpret_cast<Invoke>(worker.bridge)(worker.params, worker.entry);
  worker.returned = true;
  return 0;
}
bool SetProtection(void *address, DWORD protection) {
  DWORD old{};
  return VirtualProtectFromApp(address, 4096, protection, &old) != FALSE;
}
bool IsProtection(void *address, DWORD expected) {
  MEMORY_BASIC_INFORMATION info{};
  return VirtualQuery(address, &info, sizeof(info)) &&
         info.State == MEM_COMMIT && info.Protect == expected;
}
} // namespace

bool NativeEntryResult::DiagnosticPassed() const {
  return plan_verified && entered && stopped_at_import && capture_verified &&
         permissions_verified && returned_to_host && worker_joined && cleanup &&
         error == ERROR_SUCCESS && blocker == "first_import_not_implemented";
}
std::string NativeEntryResult::Details() const {
  std::ostringstream out;
  out << "stage=native_entry;execution_scope=verified_crt_prefix_to_first_"
         "import"
         ";execution_backend=native_x86_64;interpreter=0;entry_plan_verified="
      << plan_verified << ";guest_entry_called=" << entered
      << ";guest_executed=" << entered
      << ";game_executed=" << (title_input && entered)
      << ";game_boot_completed=0;game_frame=0;first_import_reached="
      << stopped_at_import << ";capture_verified=" << capture_verified
      << ";host_return_verified=" << returned_to_host
      << ";worker_joined=" << worker_joined << ";cleanup_verified=" << cleanup
      << ";permissions_verified=" << permissions_verified
      << ";entry_address=" << entry << ";first_plt_address=" << plt
      << ";first_slot_address=" << slot << ";load_bias=" << mapped_bias
      << ";address_scope=actual_owned_mapping;relative_applied="
      << relative_applied << ";blocker=" << blocker
      << ";first_import_key=" << import_key
      << ";first_import_name=" << import_name
      << ";first_import_return_ip=" << capture.return_ip
      << ";argc_observed=" << capture.argc << ";argv_observed=" << capture.argv
      << ";guest_rsp=" << capture.guest_rsp
      << ";guest_stack=windows_worker;orbis_stack_seed=two_entryparams_qwords"
         ";imports_scope=diagnostic_stop_not_hle;hle_calls=0;fake_success=0"
         ";guest_bytes_rewritten=0;tls_access_executed=0;tls_bound=0"
         ";general_guest_execution_supported=0;general_faults_supported=0"
         ";upstream_linker_execute_integrated=0;win32_error="
      << error;
  return out.str();
}

NativeEntryResult RunNativeEntryPrefix(std::span<const std::uint8_t> snapshot,
                                       void *mapping, std::size_t mapped_bytes,
                                       Core::EntryParams *params,
                                       bool title_input) {
  using namespace Core::Uwp;
  NativeEntryResult out;
  out.title_input = title_input;
  const auto fail = [&](const char *blocker, DWORD error) {
    out.blocker = blocker;
    out.error = error;
    return out;
  };
  OwnedCode thunk;
  try {
    const auto payload = StageGuestPayload(snapshot);
    const auto manifest = InspectGuestLinkManifest(snapshot, true);
    const auto plan = PlanNativeEntry(payload, manifest);
    if (!plan.valid)
      return fail(plan.error.c_str(), ERROR_NOT_SUPPORTED);
    if (!mapping || !params || mapped_bytes != payload.image.bytes.size())
      return fail("native_entry_backing_invalid", ERROR_INVALID_PARAMETER);
    const auto base = reinterpret_cast<std::uint64_t>(mapping);
    if (base % GuestPageSize || base < payload.image.plan.base ||
        mapped_bytes > UINT64_MAX - base)
      return fail("native_entry_backing_alignment_or_range",
                  ERROR_INVALID_ADDRESS);
    out.mapped_bias = base - payload.image.plan.base;
    out.entry = base + plan.entry_offset;
    out.plt = base + plan.plt_offset;
    out.slot = base + plan.slot_offset;
    // Caller provides owned, prepared data. Verify the exact data-only image
    // before selecting any executable page; no writable guest can race us.
    const auto baseline = StageGuestDataLink(snapshot, out.mapped_bias);
    if (!baseline.valid || params->argc != 1 || params->padding ||
        !params->argv[0] || params->argv[1] || params->entry_addr != out.entry)
      return fail("native_entry_prepared_state_invalid", ERROR_INVALID_DATA);
    const auto pages = StartupPageFlags(payload.image.plan);
    auto *mapped = static_cast<std::uint8_t *>(mapping);
    for (std::size_t i = 0; i < pages.size(); ++i) {
      const DWORD expected = !pages[i]      ? PAGE_NOACCESS
                             : pages[i] & 2 ? PAGE_READWRITE
                                            : PAGE_READONLY;
      if (!IsProtection(mapped + i * 4096, expected))
        return fail("native_entry_initial_permissions_invalid",
                    ERROR_INVALID_DATA);
      // Avoid reading inaccessible holes while still validating every mapped
      // page against the immutable snapshot and the actual-bias relocations.
      if (pages[i] &&
          std::memcmp(mapped + i * 4096,
                      baseline.payload.image.bytes.data() + i * 4096,
                      4096) != 0)
        return fail("native_entry_initial_bytes_mismatch", ERROR_INVALID_DATA);
    }
    thunk.base = VirtualAllocFromApp(nullptr, 4096, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!thunk.base)
      return fail("native_entry_bridge_allocation_failed", GetLastError());
    const auto capture = reinterpret_cast<std::uint64_t>(&out.capture);
    auto bridge = MakeNativeEntryBridge(capture, 0);
    const auto stop =
        reinterpret_cast<std::uint64_t>(thunk.base) + bridge.stop_offset;
    bridge = MakeNativeEntryBridge(capture, stop);
    if (bridge.code.size() > 4096)
      return fail("native_entry_bridge_budget", ERROR_INVALID_DATA);
    std::memcpy(thunk.base, bridge.code.data(), bridge.code.size());
    const std::array exports{GuestDataExport{plan.import, stop}};
    const auto linked =
        StageGuestDataLink(snapshot, out.mapped_bias, false, exports);
    if (!linked.valid || !linked.writes_verified ||
        !linked.untouched_verified ||
        linked.imports.addresses_by_symbol[plan.symbol] != stop)
      return fail("native_entry_stop_binding_failed", ERROR_INVALID_DATA);
    // Register only the selected import stop address. Never provide dummy data
    // objects or default successful services for the other unresolved imports.
    out.import_key = plan.import.Label();
    if (const auto *nid = Core::AeroLib::FindByNid(plan.import.nid.c_str()))
      out.import_name = nid->name;
    else
      out.import_name = "unknown_nid";
    auto *slot_page = mapped + (plan.slot_offset & ~std::uint64_t{4095});
    const auto slot_protection =
        pages[plan.slot_offset / 4096] & 2 ? PAGE_READWRITE : PAGE_READONLY;
    auto *entry_page = mapped + (plan.entry_offset & ~std::uint64_t{4095});
    auto *plt_page = mapped + (plan.plt_offset & ~std::uint64_t{4095});
    if (!SetProtection(slot_page, PAGE_READWRITE))
      return fail("native_entry_slot_protection_failed", GetLastError());
    std::uint64_t original{};
    std::memcpy(&original, mapped + plan.slot_offset, sizeof(original));
    std::memcpy(mapped + plan.slot_offset, &stop, sizeof(stop));
    bool executable = false;
    // After this point cleanup runs for every ordinary setup/run failure.
    const auto restore = [&] {
      bool ok = SetProtection(entry_page, PAGE_READONLY);
      ok = SetProtection(plt_page, PAGE_READONLY) && ok;
      const bool slot_writable = SetProtection(slot_page, PAGE_READWRITE);
      if (slot_writable)
        std::memcpy(mapped + plan.slot_offset, &original, sizeof(original));
      ok = slot_writable && ok;
      ok = SetProtection(slot_page, slot_protection) && ok;
      if (executable)
        ok = SetProtection(thunk.base, PAGE_READWRITE) && ok;
      out.cleanup = ok && IsProtection(entry_page, PAGE_READONLY) &&
                    IsProtection(plt_page, PAGE_READONLY) &&
                    IsProtection(slot_page, slot_protection) &&
                    std::memcmp(mapped + plan.slot_offset, &original,
                                sizeof(original)) == 0;
      if (thunk.base && VirtualFree(thunk.base, 0, MEM_RELEASE))
        thunk.base = nullptr;
      else
        out.cleanup = false;
    };
    if (!SetProtection(thunk.base, PAGE_EXECUTE_READ) ||
        !SetProtection(entry_page, PAGE_EXECUTE_READ) ||
        !SetProtection(plt_page, PAGE_EXECUTE_READ) ||
        !SetProtection(slot_page, slot_protection) ||
        !FlushInstructionCache(GetCurrentProcess(), thunk.base,
                               bridge.code.size()) ||
        !FlushInstructionCache(GetCurrentProcess(), entry_page, 4096) ||
        !FlushInstructionCache(GetCurrentProcess(), plt_page, 4096)) {
      const auto error = GetLastError();
      restore();
      return fail("native_entry_executable_protection_failed", error);
    }
    executable = true;
    out.plan_verified = true;
    out.relative_applied = linked.relative_applied;
    out.permissions_verified =
        IsProtection(thunk.base, PAGE_EXECUTE_READ) &&
        IsProtection(entry_page, PAGE_EXECUTE_READ) &&
        IsProtection(plt_page, PAGE_EXECUTE_READ) &&
        IsProtection(slot_page, slot_protection) &&
        std::memcmp(mapped + plan.slot_offset, &stop, sizeof(stop)) == 0;
    NativeWorker worker{params, reinterpret_cast<void *>(out.entry),
                        thunk.base};
    HANDLE thread = out.permissions_verified
                        ? CreateThread(nullptr, 0, Enter, &worker, 0, nullptr)
                        : nullptr;
    if (!thread) {
      const auto error = GetLastError();
      restore();
      return fail("native_entry_worker_creation_failed", error);
    }
    // The certified path has no loops or host calls. Join before freeing ANY
    // backing. No TerminateThread and no false cancellation of live guest code.
    const auto wait = WaitForSingleObject(thread, INFINITE);
    out.worker_joined = wait == WAIT_OBJECT_0;
    if (!out.worker_joined) {
      // A valid exclusively owned handle cannot normally fail. Fail the host
      // process rather than freeing backing referenced by a possibly live CPU.
      std::terminate();
    }
    const bool handle_closed = CloseHandle(thread) != FALSE;
    out.entered = out.capture.reached == 1;
    out.stopped_at_import =
        out.entered && out.capture.return_ip == out.entry + 25;
    out.returned_to_host = worker.returned && worker.thread_id != 0;
    out.capture_verified =
        out.stopped_at_import && out.capture.argc == 1 &&
        out.capture.argv == reinterpret_cast<std::uint64_t>(&params->argv[0]) &&
        out.capture.exit_function == stop &&
        out.capture.arguments[0] == reinterpret_cast<std::uint64_t>(params) &&
        out.capture.arguments[1] == stop && out.capture.guest_rsp % 16 == 8 &&
        out.capture.guest_rsp > worker.stack_low &&
        out.capture.saved_host_rsp < worker.stack_high &&
        out.capture.saved_host_rsp > out.capture.guest_rsp &&
        out.capture.saved_host_rsp - out.capture.guest_rsp == 72;
    restore();
    out.cleanup = out.cleanup && handle_closed;
    out.blocker = "first_import_not_implemented";
    if (!out.DiagnosticPassed())
      return fail("native_entry_capture_or_cleanup_failed", ERROR_INVALID_DATA);
    return out;
  } catch (...) {
    return fail("native_entry_setup_exception", ERROR_GEN_FAILURE);
  }
}

bool NativeEntryGatePassed() noexcept { return gate_passed.load(); }
XboxSeriesD3D12::Phase0::ProbeResult ProbeNativeEntry() {
  bool passed = true;
  std::string details;
  for (const bool self : {false, true}) {
    GuestStartupSession session;
    const auto file = Core::Uwp::MakeNativeEntryFixture(self);
    const bool prepared = session.Prepare(file);
    const auto result = session.AttemptNativeEntry(file, true);
    const auto repeated = session.AttemptNativeEntry(file, true);
    passed = prepared && result.DiagnosticPassed() &&
             repeated.DiagnosticPassed() && session.Release() && passed;
    details = result.Details();
  }
  GuestStartupSession rejected;
  auto invalid = Core::Uwp::MakeNativeEntryFixture(false);
  invalid[0x400] ^= 1; // Different instruction must be rejected, not executed.
  const bool prepared = rejected.Prepare(invalid);
  const auto negative = rejected.AttemptNativeEntry(invalid, true);
  passed = prepared && !negative.entered && !negative.plan_verified &&
           negative.blocker == "native_entry_prefix_unsupported" &&
           negative.error == ERROR_NOT_SUPPORTED && rejected.Release() &&
           passed;
  gate_passed.store(passed);
  return {"native-entry-boundary", passed,
          passed ? DWORD{ERROR_SUCCESS} : DWORD{ERROR_INVALID_DATA},
          details + ";source=authored_raw_self;repeat_and_rejection_verified=" +
              std::to_string(passed) +
              ";native_entry_fixture_verified=" + std::to_string(passed)};
}
