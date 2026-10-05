// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_execution_probe.h"
#include "core/uwp/guest_hle_fixture.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <new>
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
struct FunctionTable {
  RUNTIME_FUNCTION *entry{};
  Allocation *allocation{};
  bool registered{};
  bool Remove() {
    if (!registered)
      return true;
    registered = false;
    if (RtlDeleteFunctionTable(entry))
      return true;
    // Never free code/table still referenced by the OS. Fail and keep it alive.
    allocation->base = nullptr;
    return false;
  }
  ~FunctionTable() { (void)Remove(); }
};
struct WorkerState {
  std::uint64_t saved_rsp{};
  std::uintptr_t fault_ip{}, guard_address{};
  ULONG_PTR stack_low{}, stack_high{};
  std::uint64_t value{}, recovery_value{};
  DWORD error{}, fault_code{};
  bool armed{}, recovered{}, protections{}, stack_verified{}, cleanup{},
      filter_verified{};
  bool win64_recovered{}, sysv_recovered{}, unwind_verified{},
      table_registered{}, table_removed{};
  bool hle_verified{}, hle_unwind_verified{}, hle_table_removed{};
  std::uint64_t hle_value{}, hle_unknown{}, hle_overflow{};
};
std::uint64_t FixtureHleCallback(std::uint64_t operation,
                                 std::uint64_t argument) noexcept {
  return DispatchFixtureHle(operation, argument);
}
LONG FixtureException(EXCEPTION_POINTERS *exception, WorkerState *state) {
  if (!state->armed || state->recovered ||
      exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      exception->ContextRecord->Rip != state->fault_ip ||
      exception->ExceptionRecord->NumberParameters < 2 ||
      exception->ExceptionRecord->ExceptionInformation[0] != 0 ||
      exception->ExceptionRecord->ExceptionInformation[1] !=
          state->guard_address ||
      (exception->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
    return EXCEPTION_CONTINUE_SEARCH;
  state->fault_code = exception->ExceptionRecord->ExceptionCode;
  state->recovered = true;
  return EXCEPTION_EXECUTE_HANDLER;
}

bool RejectOtherFaults(WorkerState &state) {
  for (unsigned variant = 0; variant < 7; ++variant) {
    EXCEPTION_RECORD record{};
    CONTEXT context{};
    EXCEPTION_POINTERS pointers{&record, &context};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.NumberParameters = 2;
    record.ExceptionInformation[1] = state.guard_address;
    context.Rip = state.fault_ip;
    state.armed = true;
    switch (variant) {
    case 0:
      state.armed = false;
      break;
    case 1:
      context.Rip++;
      break;
    case 2:
      record.ExceptionInformation[1]++;
      break;
    case 3:
      record.ExceptionInformation[0] = 1;
      break;
    case 4:
      record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
      break;
    case 5:
      record.NumberParameters = 0;
      break;
    case 6:
      record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
      break;
    }
    const bool rejected =
        FixtureException(&pointers, &state) == EXCEPTION_CONTINUE_SEARCH &&
        !state.recovered;
    state.armed = false;
    if (!rejected)
      return false;
  }
  return true;
}

// Compiler-generated SEH metadata on this native frame. The fault fixture is
// a Win64 leaf (no stack/nonvolatile changes), so no dynamic unwind table is
// needed.
void InvokeFaultLeaf(const void *entry, const void *guard, WorkerState *state) {
  __try {
    using Leaf = std::uint64_t (*)(const void *);
    (void)reinterpret_cast<Leaf>(entry)(guard);
  } __except (FixtureException(GetExceptionInformation(), state)) {
    // Only the exact expected read fault reaches this clause.
  }
}

using BridgeFunction = std::uint64_t (*)(std::uint64_t, std::uint64_t,
                                         const void *, std::uint64_t *);
void InvokeSysvFault(BridgeFunction bridge, const void *entry,
                     const void *guard, WorkerState *state) {
  __try {
    (void)bridge(reinterpret_cast<std::uintptr_t>(guard), 0, entry,
                 &state->saved_rsp);
  } __except (FixtureException(GetExceptionInformation(), state)) {
    // Dynamic metadata unwinds the bridge into this compiler-generated SEH
    // frame.
  }
}

bool VerifyUnwind(void *base, RUNTIME_FUNCTION *entry,
                  const FixtureBridge &bridge) {
  const auto address = reinterpret_cast<DWORD64>(base);
  DWORD64 found_base{};
  const auto *found = RtlLookupFunctionEntry(address + bridge.epilogue_offset,
                                             &found_base, nullptr);
  if (!found || found_base != address ||
      found->BeginAddress != entry->BeginAddress ||
      found->EndAddress != entry->EndAddress ||
      found->UnwindData != entry->UnwindData)
    return false;
  alignas(16) std::array<std::uint64_t, 40> stack{};
  for (unsigned reg = 6; reg < 16; ++reg) {
    stack[16 + (reg - 6) * 2] = 1000 + reg;
    stack[17 + (reg - 6) * 2] = 2000 + reg;
  }
  stack[37] = 0x11223344; // saved RSI
  stack[38] = 0x55667788; // saved RDI
  stack[39] =
      0x12345678; // caller return address (only simulated, never jumped to)
  CONTEXT context{};
  context.ContextFlags = CONTEXT_FULL;
  context.Rsp = reinterpret_cast<DWORD64>(stack.data());
  context.Rip = address + bridge.epilogue_offset;
  void *handler_data{};
  DWORD64 establisher{};
  (void)RtlVirtualUnwind(0, address, context.Rip, entry, &context,
                         &handler_data, &establisher, nullptr);
  if (context.Rsp != reinterpret_cast<DWORD64>(stack.data()) + sizeof(stack) ||
      context.Rip != stack[39] || context.Rsi != stack[37] ||
      context.Rdi != stack[38])
    return false;
  static_assert(offsetof(CONTEXT, Xmm15) ==
                offsetof(CONTEXT, Xmm6) + 9 * sizeof(M128A));
  for (unsigned reg = 6; reg < 16; ++reg) {
    M128A value{};
    std::memcpy(&value,
                reinterpret_cast<const std::uint8_t *>(&context) +
                    offsetof(CONTEXT, Xmm6) + (reg - 6) * sizeof(M128A),
                sizeof(value));
    if (value.Low != 1000 + reg ||
        value.High != static_cast<std::int64_t>(2000 + reg))
      return false;
  }
  return true;
}

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
    const auto file = MakeGuestHleFixture();
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
    Allocation image, thunk, hle;
    image.base = VirtualAllocFromApp(nullptr, staged.bytes.size() + 2 * page,
                                     MEM_RESERVE, PAGE_NOACCESS);
    thunk.base = VirtualAllocFromApp(nullptr, page, MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    hle.base = VirtualAllocFromApp(nullptr, page, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (!image.base || !thunk.base || !hle.base) {
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
    if (bridge.code.size() > FixtureRuntimeTableOffset ||
        FixtureRuntimeTableOffset + sizeof(RUNTIME_FUNCTION) >
            FixtureUnwindOffset ||
        FixtureUnwindOffset + bridge.unwind.size() > page) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    std::memcpy(thunk.base, bridge.code.data(), bridge.code.size());
    auto *function = new (static_cast<std::uint8_t *>(thunk.base) +
                          FixtureRuntimeTableOffset) RUNTIME_FUNCTION{};
    function->BeginAddress = 0;
    function->EndAddress = static_cast<DWORD>(bridge.code.size());
    function->UnwindData = FixtureUnwindOffset;
    std::memcpy(static_cast<std::uint8_t *>(thunk.base) + FixtureUnwindOffset,
                bridge.unwind.data(), bridge.unwind.size());
    const auto hle_thunk = MakeFixtureHleThunk(
        reinterpret_cast<std::uint64_t>(&FixtureHleCallback));
    std::memcpy(hle.base, hle_thunk.code.data(), hle_thunk.code.size());
    auto *hle_function =
        new (static_cast<std::uint8_t *>(hle.base) + FixtureRuntimeTableOffset)
            RUNTIME_FUNCTION{};
    hle_function->EndAddress = static_cast<DWORD>(hle_thunk.code.size());
    hle_function->UnwindData = FixtureUnwindOffset;
    std::memcpy(static_cast<std::uint8_t *>(hle.base) + FixtureUnwindOffset,
                hle_thunk.unwind.data(), hle_thunk.unwind.size());
    const auto hle_address = reinterpret_cast<std::uint64_t>(hle.base);
    std::memcpy(payload + FixtureHlePointerOffset, &hle_address,
                sizeof(hle_address));
    if (!Protect(payload, staged.bytes.size(), PAGE_NOACCESS) ||
        !Protect(payload, page, PAGE_EXECUTE_READ) ||
        !Protect(payload + 0x4000, page, PAGE_READWRITE) ||
        !Protect(thunk.base, page, PAGE_EXECUTE_READ) ||
        !Protect(hle.base, page, PAGE_EXECUTE_READ) ||
        !FlushInstructionCache(GetCurrentProcess(), payload, page) ||
        !FlushInstructionCache(GetCurrentProcess(), thunk.base,
                               bridge.code.size()) ||
        !FlushInstructionCache(GetCurrentProcess(), hle.base,
                               hle_thunk.code.size())) {
      state.error = GetLastError();
      return 0;
    }
    state.protections =
        IsProtection(payload, PAGE_EXECUTE_READ) &&
        IsProtection(payload + 0x4000, PAGE_READWRITE) &&
        IsProtection(payload + page, PAGE_NOACCESS) &&
        IsProtection(thunk.base, PAGE_EXECUTE_READ) &&
        IsProtection(hle.base, PAGE_EXECUTE_READ) &&
        IsProtection(image.base, 0, MEM_RESERVE) &&
        IsProtection(payload + staged.bytes.size(), 0, MEM_RESERVE);
    if (!state.protections) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    FunctionTable table{function, &thunk, false};
    if (!RtlAddFunctionTable(function, 1,
                             reinterpret_cast<DWORD64>(thunk.base))) {
      state.error = ERROR_NOT_SUPPORTED;
      return 0;
    }
    table.registered = true;
    state.table_registered = true;
    FunctionTable hle_table{hle_function, &hle, false};
    if (!RtlAddFunctionTable(hle_function, 1, hle_address)) {
      state.error = ERROR_NOT_SUPPORTED;
      return 0;
    }
    hle_table.registered = true;
    // Verify the HLE thunk's call-site unwind, independently of actual
    // execution.
    std::array<std::uint64_t, 6> hle_stack{};
    hle_stack[5] = 0x12345678;
    CONTEXT hle_context{};
    hle_context.ContextFlags = CONTEXT_FULL;
    hle_context.Rsp = reinterpret_cast<DWORD64>(hle_stack.data());
    hle_context.Rip = hle_address + hle_thunk.epilogue_offset - 2;
    DWORD64 hle_found_base{};
    const auto *hle_found =
        RtlLookupFunctionEntry(hle_context.Rip, &hle_found_base, nullptr);
    if (!hle_found || hle_found_base != hle_address ||
        hle_found->UnwindData != FixtureUnwindOffset) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    void *hle_handler{};
    DWORD64 hle_establisher{};
    (void)RtlVirtualUnwind(0, hle_address, hle_context.Rip, hle_function,
                           &hle_context, &hle_handler, &hle_establisher,
                           nullptr);
    state.hle_unwind_verified =
        hle_context.Rip == hle_stack[5] &&
        hle_context.Rsp ==
            reinterpret_cast<DWORD64>(hle_stack.data()) + sizeof(hle_stack);
    if (!state.hle_unwind_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    state.unwind_verified = VerifyUnwind(thunk.base, function, bridge);
    if (!state.unwind_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    state.fault_ip =
        reinterpret_cast<std::uintptr_t>(payload + FixtureFaultOffset);
    state.guard_address = reinterpret_cast<std::uintptr_t>(image.base);
    const auto invoke = reinterpret_cast<BridgeFunction>(thunk.base);
    state.value =
        invoke(19, 23, payload + staged.plan.entry_offset, &state.saved_rsp);
    state.filter_verified = RejectOtherFaults(state);
    if (!state.filter_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    state.armed = true;
    InvokeFaultLeaf(payload + FixtureFaultOffset, image.base, &state);
    state.armed = false;
    state.win64_recovered = state.recovered;
    state.recovered = false;
    state.fault_ip =
        reinterpret_cast<std::uintptr_t>(payload + FixtureSysvFaultInstruction);
    state.filter_verified = state.filter_verified && RejectOtherFaults(state);
    if (!state.filter_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    state.armed = true;
    InvokeSysvFault(invoke, payload + FixtureSysvFaultOffset, image.base,
                    &state);
    state.armed = false;
    state.sysv_recovered = state.recovered;
    // Prove the return boundary still works after handling the expected fault.
    state.recovery_value =
        invoke(19, 23, payload + staged.plan.entry_offset, &state.saved_rsp);
    state.hle_value =
        invoke(1, 41, payload + FixtureHleEntryOffset, &state.saved_rsp);
    state.hle_unknown =
        invoke(99, 41, payload + FixtureHleEntryOffset, &state.saved_rsp);
    state.hle_overflow = invoke(1, UINT64_MAX, payload + FixtureHleEntryOffset,
                                &state.saved_rsp);
    state.hle_verified =
        state.hle_value == 42 && state.hle_unknown == FixtureHleUnsupported &&
        state.hle_overflow == FixtureHleOverflow &&
        invoke(1, 0, payload + FixtureHleEntryOffset, &state.saved_rsp) == 1;
    state.hle_table_removed = hle_table.Remove();
    state.table_removed = table.Remove();
    if (!state.table_removed || !state.hle_table_removed) {
      state.error = ERROR_FUNCTION_FAILED;
      return 0;
    }
    const bool image_freed = VirtualFree(image.base, 0, MEM_RELEASE) != FALSE;
    if (image_freed)
      image.base = nullptr;
    const bool thunk_freed = VirtualFree(thunk.base, 0, MEM_RELEASE) != FALSE;
    if (thunk_freed)
      thunk.base = nullptr;
    const bool hle_freed = VirtualFree(hle.base, 0, MEM_RELEASE) != FALSE;
    if (hle_freed)
      hle.base = nullptr;
    state.cleanup = image_freed && thunk_freed && hle_freed;
  } catch (...) {
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
  const bool passed =
      wait == WAIT_OBJECT_0 && state.error == 0 && state.value == 42 &&
      state.recovery_value == 42 && state.recovered && state.protections &&
      state.stack_verified && state.cleanup && state.filter_verified &&
      state.win64_recovered && state.sysv_recovered && state.unwind_verified &&
      state.table_registered && state.table_removed && state.hle_verified &&
      state.hle_unwind_verified && state.hle_table_removed;
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
          << ";fault_filter_verified=" << state.filter_verified
          << ";fault_boundary=sysv_leaf_bridge_seh;sysv_fault_unwind_supported="
          << state.sysv_recovered
          << ";win64_fault_recovered=" << state.win64_recovered
          << ";sysv_fault_recovered=" << state.sysv_recovered
          << ";unwind_context_verified=" << state.unwind_verified
          << ";function_table_registered=" << state.table_registered
          << ";function_table_removed=" << state.table_removed
          << ";unwind_scope=authored_leaf_and_fixed_bridge"
          << ";fault_code=" << state.fault_code
          << ";hle_scope=authored_integer_tailcall;orbis_hle_linked=0"
          << ";hle_roundtrip_verified=" << state.hle_verified
          << ";hle_return=" << state.hle_value << ";hle_unknown_rejected="
          << (state.hle_unknown == FixtureHleUnsupported)
          << ";hle_overflow_rejected="
          << (state.hle_overflow == FixtureHleOverflow)
          << ";hle_unwind_verified=" << state.hle_unwind_verified
          << ";hle_table_removed=" << state.hle_table_removed
          << ";allocations_released=" << state.cleanup;
  return {"guest-execution-fixture", passed,
          passed ? DWORD{ERROR_SUCCESS}
                 : (state.error ? state.error : DWORD{ERROR_INVALID_DATA}),
          details.str()};
}
