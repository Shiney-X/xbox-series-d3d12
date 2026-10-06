// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "guest_execution_probe.h"
#include "core/uwp/guest_hle_fixture.h"
#include "core/uwp/guest_hle_import_fixture.h"
#include "core/uwp/guest_memory_fixture.h"
#include "core/uwp/kernel_clock_fixture.h"
#include "guest_startup_session.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <memory>
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
  bool import_link_verified{}, import_slot_verified{},
      import_relative_verified{}, import_missing_rejected{},
      import_version_rejected{}, import_type_rejected{};
  std::uint64_t hle_value{}, hle_unknown{}, hle_overflow{};
  DWORD tls_index{TLS_OUT_OF_INDEXES};
  HANDLE ready{}, release{};
  unsigned ordinal{};
  FixtureThreadContext context{};
  bool tls_bound{}, tls_cleared{}, context_verified{}, rendezvous{};
};
struct ThreadBinding {
  WorkerState &state;
  ~ThreadBinding() {
    if (state.tls_bound)
      state.tls_cleared = TlsSetValue(state.tls_index, nullptr) != FALSE &&
                          TlsGetValue(state.tls_index) == nullptr;
    // Wake the coordinator on early failure so it can release/join peers.
    if (state.ready)
      SetEvent(state.ready);
  }
};
std::uint64_t FixtureHleCallback(std::uint64_t operation,
                                 std::uint64_t argument,
                                 std::uint64_t key) noexcept {
  if (key >= TLS_OUT_OF_INDEXES)
    return FixtureHleMissingContext;
  auto *context =
      static_cast<FixtureThreadContext *>(TlsGetValue(static_cast<DWORD>(key)));
  if (!context || context->owner != GetCurrentThreadId())
    return FixtureHleMissingContext;
  return DispatchFixtureContext(*context, operation, argument);
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

struct KernelClockContext {
  KernelClock clock;
  DWORD owner{};
  bool failed{};
  std::uint64_t calls{};
};
std::uint64_t ReadKernelClock(KernelClockContext *context,
                              KernelClockService service) noexcept {
  if (!context || context->owner != GetCurrentThreadId())
    return 0;
  ++context->calls;
  LARGE_INTEGER now{};
  std::uint64_t value{};
  if (!QueryPerformanceCounter(&now) || now.QuadPart < 0 ||
      !context->clock.Read(static_cast<std::uint64_t>(now.QuadPart), service,
                           value)) {
    context->failed = true;
    return 0; // Harness must fail; this is not a runtime success stub.
  }
  return value;
}
std::uint64_t KernelProcessTime(KernelClockContext *context) noexcept {
  return ReadKernelClock(context, KernelClockService::Microseconds);
}
std::uint64_t KernelProcessCounter(KernelClockContext *context) noexcept {
  return ReadKernelClock(context, KernelClockService::Counter);
}
std::uint64_t KernelProcessFrequency(KernelClockContext *context) noexcept {
  return ReadKernelClock(context, KernelClockService::Frequency);
}

// Real Orbis clock APIs, but only called by a closed generated corpus. They do
// not register exports for games, create a general runtime or read guest
// memory.
bool VerifyKernelClockImports(std::uint64_t &frequency, const char *&stage) {
  stage = "qpc_initialize";
  LARGE_INTEGER rate{}, epoch{};
  if (!QueryPerformanceFrequency(&rate) || !QueryPerformanceCounter(&epoch) ||
      rate.QuadPart <= 0 || epoch.QuadPart < 0)
    return false;
  KernelClockContext context{{static_cast<std::uint64_t>(rate.QuadPart),
                              static_cast<std::uint64_t>(epoch.QuadPart)},
                             GetCurrentThreadId(),
                             false,
                             0};
  if (!context.clock.Valid())
    return false;
  frequency = context.clock.frequency;
  const std::array callbacks{&KernelProcessTime, &KernelProcessCounter,
                             &KernelProcessFrequency};
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  if (info.dwPageSize != 4096)
    return false;
  constexpr SIZE_T page = 4096;
  for (const bool self : {false, true}) {
    stage = "fixture_staging";
    const auto file = MakeKernelClockFixture(self);
    const auto staged = StageGuestPayload(file);
    if (!staged.Ready() || staged.image.bytes.size() != 32768)
      return false;
    Allocation image, bridge_memory, services;
    stage = "owned_allocations";
    image.base = VirtualAllocFromApp(nullptr, 32768 + 2 * page, MEM_RESERVE,
                                     PAGE_NOACCESS);
    bridge_memory.base = VirtualAllocFromApp(
        nullptr, page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    services.base = VirtualAllocFromApp(nullptr, page, MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    if (!image.base || !bridge_memory.base || !services.base)
      return false;
    auto *payload = static_cast<std::uint8_t *>(image.base) + page;
    if (VirtualAllocFromApp(payload, 32768, MEM_COMMIT, PAGE_READWRITE) !=
            payload ||
        reinterpret_cast<std::uint64_t>(payload) < staged.image.plan.base)
      return false;
    const auto bridge = MakeFixtureBridge();
    if (bridge.code.size() > FixtureRuntimeTableOffset ||
        FixtureRuntimeTableOffset + sizeof(RUNTIME_FUNCTION) >
            FixtureUnwindOffset ||
        FixtureUnwindOffset + bridge.unwind.size() > page)
      return false;
    std::memcpy(bridge_memory.base, bridge.code.data(), bridge.code.size());
    auto *bridge_function =
        new (static_cast<std::uint8_t *>(bridge_memory.base) +
             FixtureRuntimeTableOffset) RUNTIME_FUNCTION{};
    bridge_function->EndAddress = static_cast<DWORD>(bridge.code.size());
    bridge_function->UnwindData = FixtureUnwindOffset;
    std::memcpy(static_cast<std::uint8_t *>(bridge_memory.base) +
                    FixtureUnwindOffset,
                bridge.unwind.data(), bridge.unwind.size());
    auto *functions = reinterpret_cast<RUNTIME_FUNCTION *>(
        static_cast<std::uint8_t *>(services.base) + 512);
    std::array<GuestDataExport, 3> registry;
    std::array<FixtureBridge, 3> thunks;
    for (unsigned i = 0; i < 3; ++i) {
      thunks[i] =
          MakeKernelClockThunk(reinterpret_cast<std::uint64_t>(callbacks[i]),
                               reinterpret_cast<std::uint64_t>(&context));
      if (thunks[i].code.size() > 64 || thunks[i].unwind.size() != 8)
        return false;
      auto *code = static_cast<std::uint8_t *>(services.base) + i * 64;
      std::memcpy(code, thunks[i].code.data(), thunks[i].code.size());
      new (&functions[i]) RUNTIME_FUNCTION{
          i * 64, i * 64 + static_cast<DWORD>(thunks[i].code.size()),
          560 + i * 8};
      std::memcpy(static_cast<std::uint8_t *>(services.base) +
                      functions[i].UnwindData,
                  thunks[i].unwind.data(), 8);
      registry[i] = {KernelClockKey(i), reinterpret_cast<std::uint64_t>(code)};
    }
    const auto bias =
        reinterpret_cast<std::uint64_t>(payload) - staged.image.plan.base;
    stage = "strict_import_link";
    const auto linked = StageGuestDataLink(file, bias, true, registry);
    if (!linked.valid || !linked.complete || linked.imports.matched != 3 ||
        linked.relative_applied != 1 || linked.data_import_applied != 3 ||
        !linked.writes_verified || !linked.untouched_verified)
      return false;
    stage = "mapped_readback_and_protections";
    std::memcpy(payload, linked.payload.image.bytes.data(), 32768);
    if (std::memcmp(payload, linked.payload.image.bytes.data(), 32768) != 0 ||
        !Protect(payload, 32768, PAGE_NOACCESS) ||
        !Protect(payload, page, PAGE_EXECUTE_READ) ||
        !Protect(payload + 0x4000, page, PAGE_READWRITE) ||
        !Protect(bridge_memory.base, page, PAGE_EXECUTE_READ) ||
        !Protect(services.base, page, PAGE_EXECUTE_READ) ||
        !IsProtection(payload, PAGE_EXECUTE_READ) ||
        !IsProtection(payload + 0x4000, PAGE_READWRITE) ||
        !IsProtection(payload + page, PAGE_NOACCESS) ||
        !IsProtection(bridge_memory.base, PAGE_EXECUTE_READ) ||
        !IsProtection(services.base, PAGE_EXECUTE_READ) ||
        !IsProtection(image.base, 0, MEM_RESERVE) ||
        !IsProtection(payload + 32768, 0, MEM_RESERVE) ||
        !FlushInstructionCache(GetCurrentProcess(), payload, page) ||
        !FlushInstructionCache(GetCurrentProcess(), bridge_memory.base,
                               bridge.code.size()) ||
        !FlushInstructionCache(GetCurrentProcess(), services.base, page))
      return false;
    FunctionTable bridge_table{bridge_function, &bridge_memory, false};
    stage = "unwind_registration_and_verification";
    FunctionTable services_table{functions, &services, false};
    const auto service_base = reinterpret_cast<DWORD64>(services.base);
    if (!RtlAddFunctionTable(bridge_function, 1,
                             reinterpret_cast<DWORD64>(bridge_memory.base)))
      return false;
    bridge_table.registered = true;
    if (!RtlAddFunctionTable(functions, 3, service_base))
      return false;
    services_table.registered = true;
    if (!VerifyUnwind(bridge_memory.base, bridge_function, bridge))
      return false;
    for (unsigned i = 0; i < 3; ++i) {
      std::array<std::uint64_t, 6> stack{};
      stack[5] = 0x12345678;
      CONTEXT unwind{};
      unwind.ContextFlags = CONTEXT_FULL;
      unwind.Rsp = reinterpret_cast<DWORD64>(stack.data());
      unwind.Rip = service_base + functions[i].BeginAddress +
                   thunks[i].epilogue_offset - 2;
      DWORD64 found_base{};
      const auto *found =
          RtlLookupFunctionEntry(unwind.Rip, &found_base, nullptr);
      if (!found || found_base != service_base ||
          found->BeginAddress != functions[i].BeginAddress ||
          found->UnwindData != functions[i].UnwindData)
        return false;
      void *handler{};
      DWORD64 establisher{};
      (void)RtlVirtualUnwind(0, service_base, unwind.Rip, &functions[i],
                             &unwind, &handler, &establisher, nullptr);
      if (unwind.Rip != stack[5] ||
          unwind.Rsp != reinterpret_cast<DWORD64>(stack.data()) + sizeof(stack))
        return false;
    }
    stage = "host_bracketed_calls";
    const auto invoke = reinterpret_cast<BridgeFunction>(bridge_memory.base);
    std::uint64_t saved_rsp{};
    for (unsigned i = 0; i < 3; ++i) {
      std::uint64_t previous{};
      for (unsigned sample = 0; sample < 2; ++sample) {
        LARGE_INTEGER before{}, after{};
        if (!QueryPerformanceCounter(&before) || before.QuadPart < 0)
          return false;
        const auto value = invoke(UINT64_MAX, UINT64_MAX,
                                  payload + 0x140 + i * 16, &saved_rsp);
        if (!QueryPerformanceCounter(&after) || after.QuadPart < 0 ||
            context.failed)
          return false;
        std::uint64_t minimum{}, maximum{};
        const auto service = static_cast<KernelClockService>(i);
        if (!context.clock.Read(static_cast<std::uint64_t>(before.QuadPart),
                                service, minimum) ||
            !context.clock.Read(static_cast<std::uint64_t>(after.QuadPart),
                                service, maximum) ||
            value < minimum || value > maximum || value < previous)
          return false;
        previous = value;
      }
    }
    stage = "unwind_and_allocation_cleanup";
    if (!services_table.Remove() || !bridge_table.Remove())
      return false;
    for (auto *allocation : {&image, &bridge_memory, &services}) {
      if (!VirtualFree(allocation->base, 0, MEM_RELEASE))
        return false;
      allocation->base = nullptr;
    }
  }
  const bool passed = !context.failed && context.calls == 12;
  if (passed)
    stage = "complete";
  return passed;
}

struct MemoryContext {
  GuestMemory memory;
  DWORD owner{};
  std::uint64_t calls{}, rejected{};
};
std::uint64_t InvokeMemory(GuestMemoryService service, std::uint64_t first,
                           std::uint64_t second, std::uint64_t count,
                           MemoryContext *context) noexcept {
  if (!context || context->owner != GetCurrentThreadId())
    return 0;
  ++context->calls;
  const auto result = context->memory.Invoke(service, first, second, count);
  if (!result.valid)
    ++context->rejected;
  // Invalid pointers are a harness rejection, not a successful libc result or
  // emulated errno. This boundary must become a runtime guest fault before
  // boot.
  return result.valid ? result.value : 0;
}
std::uint64_t MemoryCopy(std::uint64_t dest, std::uint64_t source,
                         std::uint64_t count, MemoryContext *context) noexcept {
  return InvokeMemory(GuestMemoryService::Copy, dest, source, count, context);
}
std::uint64_t MemorySet(std::uint64_t dest, std::uint64_t value,
                        std::uint64_t count, MemoryContext *context) noexcept {
  return InvokeMemory(GuestMemoryService::Set, dest, value, count, context);
}
std::uint64_t MemoryCompare(std::uint64_t first, std::uint64_t second,
                            std::uint64_t count,
                            MemoryContext *context) noexcept {
  return InvokeMemory(GuestMemoryService::Compare, first, second, count,
                      context);
}
struct MemoryProbe {
  bool passed{};
  std::uint64_t calls{}, rejected{};
  const char *stage = "not_started";
};
bool VerifyGuestMemoryImports(MemoryProbe &result) {
  const std::array callbacks{&MemoryCopy, &MemorySet, &MemoryCompare};
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  if (info.dwPageSize != 4096)
    return false;
  constexpr SIZE_T page = 4096;
  for (const bool self : {false, true}) {
    result.stage = "fixture_staging";
    const auto file = MakeGuestMemoryFixture(self);
    const auto staged = StageGuestPayload(file);
    if (!staged.Ready() || staged.image.bytes.size() != 32768)
      return false;
    Allocation image, bridge_memory, services;
    result.stage = "owned_allocations";
    image.base = VirtualAllocFromApp(nullptr, 32768 + 2 * page, MEM_RESERVE,
                                     PAGE_NOACCESS);
    bridge_memory.base = VirtualAllocFromApp(
        nullptr, page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    services.base = VirtualAllocFromApp(nullptr, page, MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
    if (!image.base || !bridge_memory.base || !services.base)
      return false;
    auto *payload = static_cast<std::uint8_t *>(image.base) + page;
    if (VirtualAllocFromApp(payload, 32768, MEM_COMMIT, PAGE_READWRITE) !=
            payload ||
        reinterpret_cast<std::uint64_t>(payload) < staged.image.plan.base)
      return false;
    // Authorize logical LOAD ranges, not rounded pages, padding, stack,
    // imports' native callbacks or arbitrary host memory. Backing cannot change
    // in this serial closed test; no check/use race with mprotect/munmap.
    const std::array ranges{GuestMemoryRange{{payload + 0x100, 0x100}, false},
                            GuestMemoryRange{{payload + 0x4200, 0x400}, true}};
    MemoryContext context{GuestMemory(ranges), GetCurrentThreadId(), 0, 0};
    if (!context.memory.Valid())
      return false;
    const auto bridge = MakeFixtureBridge();
    if (bridge.code.size() > FixtureRuntimeTableOffset ||
        FixtureRuntimeTableOffset + sizeof(RUNTIME_FUNCTION) >
            FixtureUnwindOffset ||
        FixtureUnwindOffset + bridge.unwind.size() > page)
      return false;
    std::memcpy(bridge_memory.base, bridge.code.data(), bridge.code.size());
    auto *bridge_function =
        new (static_cast<std::uint8_t *>(bridge_memory.base) +
             FixtureRuntimeTableOffset) RUNTIME_FUNCTION{};
    bridge_function->EndAddress = static_cast<DWORD>(bridge.code.size());
    bridge_function->UnwindData = FixtureUnwindOffset;
    std::memcpy(static_cast<std::uint8_t *>(bridge_memory.base) +
                    FixtureUnwindOffset,
                bridge.unwind.data(), bridge.unwind.size());
    auto *functions = reinterpret_cast<RUNTIME_FUNCTION *>(
        static_cast<std::uint8_t *>(services.base) + 512);
    std::array<GuestDataExport, 3> registry;
    std::array<FixtureBridge, 3> thunks;
    for (unsigned i = 0; i < 3; ++i) {
      thunks[i] =
          MakeGuestMemoryThunk(reinterpret_cast<std::uint64_t>(callbacks[i]),
                               reinterpret_cast<std::uint64_t>(&context));
      if (thunks[i].code.size() > 64 || thunks[i].unwind.size() != 8)
        return false;
      auto *code = static_cast<std::uint8_t *>(services.base) + i * 64;
      std::memcpy(code, thunks[i].code.data(), thunks[i].code.size());
      new (&functions[i]) RUNTIME_FUNCTION{
          i * 64, i * 64 + static_cast<DWORD>(thunks[i].code.size()),
          560 + i * 8};
      std::memcpy(static_cast<std::uint8_t *>(services.base) +
                      functions[i].UnwindData,
                  thunks[i].unwind.data(), 8);
      registry[i] = {GuestMemoryKey(i), reinterpret_cast<std::uint64_t>(code)};
    }
    result.stage = "strict_import_link";
    const auto bias =
        reinterpret_cast<std::uint64_t>(payload) - staged.image.plan.base;
    const auto linked = StageGuestDataLink(file, bias, true, registry);
    if (!linked.valid || !linked.complete || linked.imports.matched != 3 ||
        linked.relative_applied != 1 || linked.data_import_applied != 3 ||
        !linked.writes_verified || !linked.untouched_verified)
      return false;
    result.stage = "mapped_readback_and_protections";
    std::memcpy(payload, linked.payload.image.bytes.data(), 32768);
    if (std::memcmp(payload, linked.payload.image.bytes.data(), 32768) != 0 ||
        !Protect(payload, 32768, PAGE_NOACCESS) ||
        !Protect(payload, page, PAGE_EXECUTE_READ) ||
        !Protect(payload + 0x4000, page, PAGE_READWRITE) ||
        !Protect(bridge_memory.base, page, PAGE_EXECUTE_READ) ||
        !Protect(services.base, page, PAGE_EXECUTE_READ) ||
        !IsProtection(payload, PAGE_EXECUTE_READ) ||
        !IsProtection(payload + 0x4000, PAGE_READWRITE) ||
        !IsProtection(payload + page, PAGE_NOACCESS) ||
        !IsProtection(bridge_memory.base, PAGE_EXECUTE_READ) ||
        !IsProtection(services.base, PAGE_EXECUTE_READ) ||
        !IsProtection(image.base, 0, MEM_RESERVE) ||
        !IsProtection(payload + 32768, 0, MEM_RESERVE) ||
        !FlushInstructionCache(GetCurrentProcess(), payload, page) ||
        !FlushInstructionCache(GetCurrentProcess(), bridge_memory.base,
                               bridge.code.size()) ||
        !FlushInstructionCache(GetCurrentProcess(), services.base, page))
      return false;
    FunctionTable bridge_table{bridge_function, &bridge_memory, false};
    FunctionTable services_table{functions, &services, false};
    result.stage = "unwind_registration_and_verification";
    const auto service_base = reinterpret_cast<DWORD64>(services.base);
    if (!RtlAddFunctionTable(bridge_function, 1,
                             reinterpret_cast<DWORD64>(bridge_memory.base)))
      return false;
    bridge_table.registered = true;
    if (!RtlAddFunctionTable(functions, 3, service_base))
      return false;
    services_table.registered = true;
    if (!VerifyUnwind(bridge_memory.base, bridge_function, bridge))
      return false;
    for (unsigned i = 0; i < 3; ++i) {
      std::array<std::uint64_t, 6> stack{};
      stack[5] = 0x12345678;
      CONTEXT unwind{};
      unwind.ContextFlags = CONTEXT_FULL;
      unwind.Rsp = reinterpret_cast<DWORD64>(stack.data());
      unwind.Rip = service_base + functions[i].BeginAddress +
                   thunks[i].epilogue_offset - 2;
      DWORD64 found_base{};
      const auto *found =
          RtlLookupFunctionEntry(unwind.Rip, &found_base, nullptr);
      if (!found || found_base != service_base ||
          found->BeginAddress != functions[i].BeginAddress ||
          found->UnwindData != functions[i].UnwindData)
        return false;
      void *handler{};
      DWORD64 establisher{};
      (void)RtlVirtualUnwind(0, service_base, unwind.Rip, &functions[i],
                             &unwind, &handler, &establisher, nullptr);
      if (unwind.Rip != stack[5] ||
          unwind.Rsp != reinterpret_cast<DWORD64>(stack.data()) + sizeof(stack))
        return false;
    }
    result.stage = "bounded_memory_calls";
    const auto address = [](const void *pointer) {
      return static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(pointer));
    };
    const auto invoke = reinterpret_cast<BridgeFunction>(bridge_memory.base);
    std::uint64_t saved_rsp{};
    std::array<std::uint8_t, 0x400> expected;
    std::memcpy(expected.data(), payload + 0x4200, expected.size());
    for (unsigned i = 0; i < 16; ++i) {
      payload[0x4340 + i] = static_cast<std::uint8_t>(0x20 + i);
      expected[0x140 + i] = static_cast<std::uint8_t>(0x20 + i);
    }
    const auto call = [&](unsigned entry, std::uint64_t first,
                          std::uint64_t second, std::uint64_t value,
                          bool reject = false) {
      const auto calls = context.calls, rejected = context.rejected;
      const auto actual = invoke(first, second, payload + entry, &saved_rsp);
      return actual == value && context.calls == calls + 1 &&
             context.rejected == rejected + (reject ? 1 : 0) &&
             std::memcmp(payload + 0x4200, expected.data(), expected.size()) ==
                 0 &&
             std::memcmp(payload + 0x100,
                         linked.payload.image.bytes.data() + 0x100, 0x100) == 0;
    };
    const auto dest = address(payload + 0x4300);
    const auto source = address(payload + 0x4340);
    std::fill_n(expected.begin() + 0x100, 16, std::uint8_t{0xa5});
    if (!call(0x150, dest, 0x12345678a5ULL, dest))
      return false;
    std::copy_n(expected.begin() + 0x140, 16, expected.begin() + 0x100);
    if (!call(0x140, dest, source, dest) || !call(0x160, dest, source, 0))
      return false;
    --payload[0x4300];
    --expected[0x100];
    if (!call(0x160, dest, source, UINT64_MAX) || !call(0x160, source, dest, 1))
      return false;
    std::copy_n(payload + 0x100, 16, expected.begin() + 0x100);
    if (!call(0x140, dest, address(payload + 0x100), dest))
      return false;
    result.stage = "invalid_pointers_rejected_without_writes";
    if (!call(0x140, address(payload + 0x100), source, 0, true) ||
        !call(0x140, dest, address(payload + page), 0, true) ||
        !call(0x150, address(payload + page), 0xa5, 0, true) ||
        !call(0x140, address(payload + 0x45f8), source, 0, true) ||
        !call(0x140, dest, UINT64_MAX, 0, true) ||
        !call(0x140, dest + 1, dest, 0, true) ||
        !call(0x170, dest, source, 0, true) ||
        !call(0x140, address(&context), source, 0, true))
      return false;
    result.stage = "zero_length_calls";
    const auto end = address(payload + 0x4600);
    if (!call(0x180, end, end, end) || !call(0x190, end, 0, end) ||
        !call(0x1a0, end, end, 0) || context.calls != 17 ||
        context.rejected != 8)
      return false;
    result.calls += context.calls;
    result.rejected += context.rejected;
    result.stage = "unwind_and_allocation_cleanup";
    if (!services_table.Remove() || !bridge_table.Remove())
      return false;
    for (auto *allocation : {&image, &bridge_memory, &services}) {
      if (!VirtualFree(allocation->base, 0, MEM_RELEASE))
        return false;
      allocation->base = nullptr;
    }
  }
  result.stage = "complete";
  return result.calls == 34 && result.rejected == 16;
}

DWORD WINAPI ExecuteFixture(void *argument) noexcept {
  auto &state = *static_cast<WorkerState *>(argument);
  ThreadBinding binding{state};
  try {
    state.context.owner = GetCurrentThreadId();
    if (TlsGetValue(state.tls_index) != nullptr ||
        !TlsSetValue(state.tls_index, &state.context)) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    state.tls_bound = true;
    GetCurrentThreadStackLimits(&state.stack_low, &state.stack_high);
    const auto local = reinterpret_cast<std::uintptr_t>(&argument);
    state.stack_verified = local >= state.stack_low && local < state.stack_high;
    // Fixed generated corpus, one raw ELF worker and one SELF worker. No game
    // file or caller-selected key/address reaches this execution harness.
    const auto file = MakeGuestHleImportFixture(state.ordinal != 0);
    const auto staged = StageGuestPayload(file).image;
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
        reinterpret_cast<std::uint64_t>(&FixtureHleCallback), state.tls_index);
    std::memcpy(hle.base, hle_thunk.code.data(), hle_thunk.code.size());
    auto *hle_function =
        new (static_cast<std::uint8_t *>(hle.base) + FixtureRuntimeTableOffset)
            RUNTIME_FUNCTION{};
    hle_function->EndAddress = static_cast<DWORD>(hle_thunk.code.size());
    hle_function->UnwindData = FixtureUnwindOffset;
    std::memcpy(static_cast<std::uint8_t *>(hle.base) + FixtureUnwindOffset,
                hle_thunk.unwind.data(), hle_thunk.unwind.size());
    const auto hle_address = reinterpret_cast<std::uint64_t>(hle.base);
    const auto payload_address = reinterpret_cast<std::uint64_t>(payload);
    if (payload_address < staged.plan.base) {
      state.error = ERROR_INVALID_ADDRESS;
      return 0;
    }
    const auto bias = payload_address - staged.plan.base;
    const std::array registry{MakeFixtureHleImportExport(hle_address)};
    const auto rejected_without_write = [&](const GuestDataLink &rejected) {
      return !rejected.valid &&
             rejected.error == "pending_relocations_require_resolver" &&
             !rejected.relative_applied && !rejected.data_import_applied &&
             rejected.payload.image.bytes == staged.bytes;
    };
    state.import_missing_rejected =
        rejected_without_write(StageGuestDataLink(file, bias, true));
    auto wrong = registry;
    wrong[0].key.library_version = 1;
    state.import_version_rejected =
        rejected_without_write(StageGuestDataLink(file, bias, true, wrong));
    wrong[0] = registry[0];
    wrong[0].key.type = 1;
    state.import_type_rejected =
        rejected_without_write(StageGuestDataLink(file, bias, true, wrong));
    const auto linked = StageGuestDataLink(file, bias, true, registry);
    state.import_link_verified =
        VerifyFixtureHleImportLink(linked, bias, hle_address);
    if (!state.import_link_verified || !state.import_missing_rejected ||
        !state.import_version_rejected || !state.import_type_rejected) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
    // Only a complete, verified authorial link is copied. No manual GOT patch.
    std::memcpy(payload, linked.payload.image.bytes.data(),
                staged.bytes.size());
    std::uint64_t imported_address{};
    std::uint64_t relocated_entry{};
    std::memcpy(&imported_address, payload + FixtureHlePointerOffset,
                sizeof(imported_address));
    std::memcpy(&relocated_entry, payload + 0x4208, sizeof(relocated_entry));
    state.import_slot_verified =
        imported_address == hle_address &&
        std::memcmp(payload, linked.payload.image.bytes.data(),
                    staged.bytes.size()) == 0;
    state.import_relative_verified =
        relocated_entry ==
        reinterpret_cast<std::uint64_t>(payload + staged.plan.entry_offset);
    if (!state.import_slot_verified || !state.import_relative_verified) {
      state.error = ERROR_INVALID_DATA;
      return 0;
    }
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
        invoke(19, 23, reinterpret_cast<const void *>(relocated_entry),
               &state.saved_rsp);
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
        invoke(19, 23, reinterpret_cast<const void *>(relocated_entry),
               &state.saved_rsp);
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
    const auto local_value = std::uint64_t{100} + state.ordinal;
    const auto written = invoke(2, local_value, payload + FixtureHleEntryOffset,
                                &state.saved_rsp);
    if (!SetEvent(state.ready)) {
      state.error = GetLastError();
      return 0;
    }
    state.rendezvous =
        WaitForSingleObject(state.release, 10000) == WAIT_OBJECT_0;
    if (!state.rendezvous) {
      state.error = ERROR_TIMEOUT;
      return 0;
    }
    // Both threads have written distinct values before either reads back.
    const auto read =
        invoke(3, 0, payload + FixtureHleEntryOffset, &state.saved_rsp);
    const auto owner =
        invoke(4, 0, payload + FixtureHleEntryOffset, &state.saved_rsp);
    const auto invalid =
        invoke(3, 1, payload + FixtureHleEntryOffset, &state.saved_rsp);
    const auto unchanged =
        invoke(3, 0, payload + FixtureHleEntryOffset, &state.saved_rsp);
    state.context_verified =
        written == local_value && read == local_value &&
        unchanged == local_value && owner == state.context.owner &&
        invalid == FixtureHleUnsupported && state.context.calls == 9;
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
  const DWORD key = TlsAlloc();
  if (key == TLS_OUT_OF_INDEXES)
    return {
        "guest-execution-fixture", false, GetLastError(),
        "stage=fixture_execution;game_executed=0;error=tls_allocation_failed"};
  std::unique_ptr<std::array<WorkerState, 2>> states(
      new (std::nothrow) std::array<WorkerState, 2>{});
  if (!states) {
    TlsFree(key);
    return {"guest-execution-fixture", false, ERROR_NOT_ENOUGH_MEMORY,
            "stage=fixture_execution;game_executed=0;error=worker_state_"
            "allocation"};
  }
  std::array<HANDLE, 2> ready{}, threads{};
  HANDLE release = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET,
                                  EVENT_ALL_ACCESS);
  DWORD coordination_error = release ? ERROR_SUCCESS : GetLastError();
  for (unsigned i = 0; release && i < 2; ++i) {
    ready[i] = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET,
                              EVENT_ALL_ACCESS);
    if (!ready[i]) {
      coordination_error = GetLastError();
      break;
    }
    auto &worker = (*states)[i];
    worker.tls_index = key;
    worker.ordinal = i;
    worker.ready = ready[i];
    worker.release = release;
  }
  const bool missing_rejected =
      FixtureHleCallback(3, 0, key) == FixtureHleMissingContext;
  FixtureThreadContext foreign_context{UINT64_MAX, 0, 0};
  const bool foreign_rejected =
      TlsSetValue(key, &foreign_context) != FALSE &&
      FixtureHleCallback(3, 0, key) == FixtureHleMissingContext;
  const bool parent_cleared = TlsSetValue(key, nullptr) != FALSE;
  const bool invalid_key_rejected =
      FixtureHleCallback(3, 0, TLS_OUT_OF_INDEXES) == FixtureHleMissingContext;
  if (coordination_error == ERROR_SUCCESS) {
    for (unsigned i = 0; i < 2; ++i) {
      threads[i] =
          CreateThread(nullptr, 256 * 1024, ExecuteFixture, &(*states)[i],
                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
      if (!threads[i]) {
        coordination_error = GetLastError();
        break;
      }
    }
  }
  bool both_ready = false;
  if (threads[0] && threads[1]) {
    const auto wait = WaitForMultipleObjects(2, ready.data(), TRUE, 10000);
    both_ready = wait == WAIT_OBJECT_0;
    if (!both_ready)
      coordination_error = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
  }
  if (release && !SetEvent(release))
    coordination_error = GetLastError();
  // Only fixed authored code. Release/timeout prevents a stranded rendezvous;
  // never terminate a thread or free state/code while it might still execute.
  for (const auto thread : threads) {
    if (thread && WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
      const auto error = GetLastError();
      (void)states
          .release(); // Keep state, events and TLS key alive on join failure.
      return {"guest-execution-fixture", false, error,
              "stage=fixture_execution;game_executed=0;error=join_failed_"
              "resources_retained"};
    }
  }
  const bool parent_isolated = TlsGetValue(key) == nullptr;
  bool handles_released = true;
  for (const auto thread : threads)
    if (thread)
      handles_released = CloseHandle(thread) != FALSE && handles_released;
  for (const auto event : ready)
    if (event)
      handles_released = CloseHandle(event) != FALSE && handles_released;
  if (release)
    handles_released = CloseHandle(release) != FALSE && handles_released;
  const bool tls_released = TlsFree(key) != FALSE;
  const auto worker_passed = [](const WorkerState &state) {
    return state.error == 0 && state.value == 42 &&
           state.recovery_value == 42 && state.recovered && state.protections &&
           state.stack_verified && state.cleanup && state.filter_verified &&
           state.win64_recovered && state.sysv_recovered &&
           state.unwind_verified && state.table_registered &&
           state.table_removed && state.hle_verified &&
           state.hle_unwind_verified && state.hle_table_removed &&
           state.import_link_verified && state.import_slot_verified &&
           state.import_relative_verified && state.import_missing_rejected &&
           state.import_version_rejected && state.import_type_rejected &&
           state.tls_bound && state.tls_cleared && state.context_verified &&
           state.rendezvous;
  };
  const auto &state = (*states)[0];
  const auto &peer = (*states)[1];
  const bool contexts_isolated =
      state.context_verified && peer.context_verified &&
      state.context.owner != peer.context.owner && state.context.value == 100 &&
      peer.context.value == 101;
  std::uint64_t kernel_clock_frequency{};
  bool kernel_clock_verified{};
  const char *kernel_clock_stage = "not_started";
  try {
    kernel_clock_verified =
        VerifyKernelClockImports(kernel_clock_frequency, kernel_clock_stage);
  } catch (...) {
    kernel_clock_stage = "exception";
    kernel_clock_verified = false;
  }
  MemoryProbe memory_probe;
  try {
    memory_probe.passed = VerifyGuestMemoryImports(memory_probe);
  } catch (...) {
    memory_probe.stage = "exception";
    memory_probe.passed = false;
  }
  const auto startup_probe = ProbeGuestStartup();
  const auto native_probe = ProbeNativeEntry();
  const bool passed = native_probe.passed && startup_probe.passed &&
                      kernel_clock_verified && memory_probe.passed &&
                      coordination_error == ERROR_SUCCESS && both_ready &&
                      worker_passed(state) && worker_passed(peer) &&
                      contexts_isolated && parent_isolated &&
                      missing_rejected && foreign_rejected && parent_cleared &&
                      invalid_key_rejected && handles_released && tls_released;
  std::ostringstream details;
  details << "stage=fixture_execution;source=authored_elf;loader_linked=0;game_"
             "executed=0;game_frame=0"
          << ";guest_executed=" << (state.value == 42)
          << ";native_entry_fixture_verified=" << native_probe.passed
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
          << ";import_execution_scope=closed_authored_raw_self"
          << ";import_binding_source=typed_resolver_jump_slot"
          << ";import_manual_patch=0;game_runtime_exports_callable=0"
          << ";import_link_verified="
          << (state.import_link_verified && peer.import_link_verified)
          << ";import_slot_verified="
          << (state.import_slot_verified && peer.import_slot_verified)
          << ";import_relative_verified="
          << (state.import_relative_verified && peer.import_relative_verified)
          << ";import_call_verified="
          << (state.hle_verified && peer.hle_verified)
          << ";import_missing_rejected="
          << (state.import_missing_rejected && peer.import_missing_rejected)
          << ";import_version_rejected="
          << (state.import_version_rejected && peer.import_version_rejected)
          << ";import_type_rejected="
          << (state.import_type_rejected && peer.import_type_rejected)
          << ";thread_scope=authored_workers;worker_threads=2"
          << ";workers_passed=" << (worker_passed(state) && worker_passed(peer))
          << ";thread_context_isolated=" << contexts_isolated
          << ";tls_scope=host_slot_not_orbis;tls_parent_isolated="
          << parent_isolated
          << ";tls_missing_context_rejected=" << missing_rejected
          << ";tls_foreign_context_rejected=" << foreign_rejected
          << ";tls_invalid_key_rejected=" << invalid_key_rejected
          << ";tls_bindings_cleared=" << (state.tls_cleared && peer.tls_cleared)
          << ";tls_slot_released=" << tls_released
          << ";thread_handles_released=" << handles_released
          << ";thread_rendezvous_verified="
          << (both_ready && state.rendezvous && peer.rendezvous)
          << ";worker0_id=" << state.context.owner
          << ";worker1_id=" << peer.context.owner
          << ";worker0_tls_value=" << state.context.value
          << ";worker1_tls_value=" << peer.context.value
          << ";worker0_error=" << state.error << ";worker1_error=" << peer.error
          << ";allocations_released=" << (state.cleanup && peer.cleanup);
  details << ";kernel_clock_scope=closed_authored_raw_self;kernel_clock_"
             "backend=qpc_virtual"
          << ";kernel_clock_services=3;kernel_clock_calls_verified="
          << kernel_clock_verified
          << ";kernel_clock_units_verified=" << kernel_clock_verified
          << ";kernel_clock_unwind_cleanup_verified=" << kernel_clock_verified
          << ";kernel_clock_frequency=" << kernel_clock_frequency
          << ";kernel_clock_stage=" << kernel_clock_stage
          << ";kernel_clock_direct_rdtsc_compatible=0;game_clock_exports_"
             "registered=0";
  details
      << ";guest_memory_scope=closed_authored_raw_self;guest_memory_services=3"
      << ";guest_memory_calls_verified=" << memory_probe.passed
      << ";guest_memory_ranges_verified=" << memory_probe.passed
      << ";guest_memory_rejections_verified=" << memory_probe.passed
      << ";guest_memory_unwind_cleanup_verified=" << memory_probe.passed
      << ";guest_memory_calls=" << memory_probe.calls
      << ";guest_memory_rejected_calls=" << memory_probe.rejected
      << ";guest_memory_stage=" << memory_probe.stage
      << ";game_memory_exports_registered=0;guest_memory_general_faults_"
         "supported=0";
  details << ";startup_preparation_fixture_verified=" << startup_probe.passed
          << ";startup_preparation_scope=owned_mapping_not_entry_execution";
  return {"guest-execution-fixture", passed,
          passed ? DWORD{ERROR_SUCCESS}
                 : (coordination_error ? coordination_error
                    : state.error      ? state.error
                    : peer.error       ? peer.error
                                       : DWORD{ERROR_INVALID_DATA}),
          details.str()};
}
