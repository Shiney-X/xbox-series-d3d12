// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_execution_probe.h"
#include "core/uwp/guest_execution_fixture.h"
#include <Windows.h>
#include <cstring>
#include <sstream>

namespace {
using namespace Core::Uwp;
struct Allocation {
  void *base{};
  ~Allocation() {
    if (base)
      VirtualFree(base, 0, MEM_RELEASE);
  }
};
struct WorkerState {
  std::uint64_t saved_rsp{};
  std::uintptr_t fault_ip{}, recovery_ip{}, guard_address{};
  ULONG_PTR stack_low{}, stack_high{};
  std::uint64_t value{}, recovery_value{};
  DWORD error{}, fault_code{};
  bool armed{}, recovered{}, protections{}, stack_verified{}, cleanup{};
};
thread_local WorkerState *active_state{};

LONG CALLBACK FixtureException(EXCEPTION_POINTERS *exception) {
  auto *state = active_state;
  if (!state || !state->armed || state->recovered ||
      exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      exception->ContextRecord->Rip != state->fault_ip ||
      exception->ExceptionRecord->NumberParameters < 2 ||
      exception->ExceptionRecord->ExceptionInformation[0] != 0 ||
      exception->ExceptionRecord->ExceptionInformation[1] !=
          state->guard_address ||
      state->saved_rsp < state->stack_low + FixtureBridgeFrame ||
      state->saved_rsp >= state->stack_high)
    return EXCEPTION_CONTINUE_SEARCH;
  state->fault_code = exception->ExceptionRecord->ExceptionCode;
  state->recovered = true;
  // Continue into the owned bridge epilogue, no stack unwind through emitted
  // code.
  exception->ContextRecord->Rip = state->recovery_ip;
  exception->ContextRecord->Rsp = state->saved_rsp - FixtureBridgeFrame;
  exception->ContextRecord->Rax = 0;
  return EXCEPTION_CONTINUE_EXECUTION;
}

struct Handler {
  void *handle{};
  ~Handler() {
    if (handle)
      RemoveVectoredExceptionHandler(handle);
  }
};

bool Protect(void *address, SIZE_T bytes, ULONG flags) {
  ULONG old{};
  return VirtualProtectFromApp(address, bytes, flags, &old) != FALSE;
}
bool IsProtection(const void *address, DWORD protection,
                  DWORD state = MEM_COMMIT) {
  MEMORY_BASIC_INFORMATION info{};
  return VirtualQuery(address, &info, sizeof(info)) != 0 &&
         info.State == state &&
         (state == MEM_RESERVE || info.Protect == protection);
}

DWORD WINAPI ExecuteFixture(void *argument) noexcept {
  auto &state = *static_cast<WorkerState *>(argument);
  try {
    GetCurrentThreadStackLimits(&state.stack_low, &state.stack_high);
    const auto local = reinterpret_cast<std::uintptr_t>(&argument);
    state.stack_verified = local >= state.stack_low && local < state.stack_high;
    const auto file = MakeGuestExecutionFixture();
    const auto staged = StageRawGuest(file);
    if (!staged.error.empty() || !state.stack_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T page = info.dwPageSize;
    // This fixed fixture requires separate code/data pages, not arbitrary guest
    // mappings.
    if (page != 4096 || staged.bytes.size() != 32768) {
      state.error = ERROR_NOT_SUPPORTED;
      return 0;
    }
    Allocation image, thunk;
    image.base = VirtualAllocFromApp(nullptr, staged.bytes.size() + 2 * page,
                                     MEM_RESERVE, PAGE_NOACCESS);
    thunk.base = VirtualAllocFromApp(nullptr, page, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!image.base || !thunk.base) {
      state.error = GetLastError();
      return 0;
    }
    auto *payload = static_cast<std::uint8_t *>(image.base) + page;
    if (VirtualAllocFromApp(payload, staged.bytes.size(), MEM_COMMIT,
                            PAGE_READWRITE) != payload) {
      state.error = GetLastError();
      return 0;
    }
    std::memcpy(payload, staged.bytes.data(), staged.bytes.size());
    const auto bridge = MakeFixtureBridge();
    if (bridge.code.size() > page) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    std::memcpy(thunk.base, bridge.code.data(), bridge.code.size());
    if (!Protect(payload, staged.bytes.size(), PAGE_NOACCESS) ||
        !Protect(payload, page, PAGE_EXECUTE_READ) ||
        !Protect(payload + 0x4000, page, PAGE_READWRITE) ||
        !Protect(thunk.base, page, PAGE_EXECUTE_READ) ||
        !FlushInstructionCache(GetCurrentProcess(), payload, page) ||
        !FlushInstructionCache(GetCurrentProcess(), thunk.base,
                               bridge.code.size())) {
      state.error = GetLastError();
      return 0;
    }
    state.protections =
        IsProtection(payload, PAGE_EXECUTE_READ) &&
        IsProtection(payload + 0x4000, PAGE_READWRITE) &&
        IsProtection(payload + page, PAGE_NOACCESS) &&
        IsProtection(thunk.base, PAGE_EXECUTE_READ) &&
        IsProtection(image.base, 0, MEM_RESERVE) &&
        IsProtection(payload + staged.bytes.size(), 0, MEM_RESERVE);
    if (!state.protections) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    Handler handler{AddVectoredExceptionHandler(1, FixtureException)};
    if (!handler.handle) {
      state.error = ERROR_NOT_SUPPORTED;
      return 0;
    }
    state.fault_ip =
        reinterpret_cast<std::uintptr_t>(payload + FixtureFaultOffset);
    state.guard_address = reinterpret_cast<std::uintptr_t>(image.base);
    state.recovery_ip =
        reinterpret_cast<std::uintptr_t>(thunk.base) + bridge.recovery_offset;
    active_state = &state;
    using BridgeFunction = std::uint64_t (*)(std::uint64_t, std::uint64_t,
                                             const void *, std::uint64_t *);
    const auto invoke = reinterpret_cast<BridgeFunction>(thunk.base);
    state.value =
        invoke(19, 23, payload + staged.plan.entry_offset, &state.saved_rsp);
    state.armed = true;
    (void)invoke(state.guard_address, 0, payload + FixtureFaultOffset,
                 &state.saved_rsp);
    state.armed = false;
    // Prove the return boundary still works after handling the expected fault.
    state.recovery_value =
        invoke(19, 23, payload + staged.plan.entry_offset, &state.saved_rsp);
    active_state = nullptr;
    if (!RemoveVectoredExceptionHandler(handler.handle)) {
      state.error = ERROR_FUNCTION_FAILED;
      return 0;
    }
    handler.handle = nullptr;
    const bool image_freed = VirtualFree(image.base, 0, MEM_RELEASE) != FALSE;
    if (image_freed)
      image.base = nullptr;
    const bool thunk_freed = VirtualFree(thunk.base, 0, MEM_RELEASE) != FALSE;
    if (thunk_freed)
      thunk.base = nullptr;
    state.cleanup = image_freed && thunk_freed;
  } catch (...) {
    active_state = nullptr;
    state.error = ERROR_GEN_FAILURE;
  }
  return 0;
}
} // namespace

XboxSeriesD3D12::Phase0::ProbeResult ProbeGuestExecution() {
  WorkerState state;
  PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic{};
  PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
  if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDynamicCodePolicy,
                                  &dynamic, sizeof(dynamic)) ||
      !GetProcessMitigationPolicy(GetCurrentProcess(),
                                  ProcessControlFlowGuardPolicy, &cfg,
                                  sizeof(cfg)))
    return {"guest-execution-fixture", false, GetLastError(),
            "stage=fixture_execution;guest_executed=0;game_executed=0;error="
            "policy_query_failed"};
  if (dynamic.ProhibitDynamicCode || cfg.EnableControlFlowGuard)
    return {"guest-execution-fixture", false, ERROR_NOT_SUPPORTED,
            "stage=fixture_execution;guest_executed=0;game_executed=0;error="
            "unsupported_mitigation"};
  const HANDLE thread =
      CreateThread(nullptr, 256 * 1024, ExecuteFixture, &state,
                   STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!thread)
    return {"guest-execution-fixture", false, GetLastError(),
            "stage=fixture_execution;guest_executed=0;game_executed=0;error="
            "thread_creation_failed"};
  // Only fixed, loop-free authored code. No arbitrary entry/file is accepted.
  const DWORD wait = WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  const bool passed = wait == WAIT_OBJECT_0 && state.error == 0 &&
                      state.value == 42 && state.recovery_value == 42 &&
                      state.recovered && state.protections &&
                      state.stack_verified && state.cleanup;
  std::ostringstream details;
  details << "stage=fixture_execution;source=authored_elf;loader_linked=0;game_"
             "executed=0;game_frame=0"
          << ";guest_executed=" << (state.value == 42)
          << ";abi=sysv_integer_leaf;arguments=19,23"
          << ";return_value=" << state.value
          << ";return_after_fault=" << state.recovery_value
          << ";alignment_red_zone_verified=" << (state.value == 42)
          << ";host_permissions_verified=" << state.protections
          << ";stack=windows_worker_thread;stack_verified="
          << state.stack_verified << ";fault_recovered=" << state.recovered
          << ";fault_code=" << state.fault_code
          << ";allocations_released=" << state.cleanup;
  return {"guest-execution-fixture", passed,
          passed ? DWORD{ERROR_SUCCESS}
                 : (state.error ? state.error : DWORD{ERROR_INVALID_DATA}),
          details.str()};
}
