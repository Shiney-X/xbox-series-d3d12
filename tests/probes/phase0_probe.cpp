// SPDX-License-Identifier: GPL-2.0-or-later

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ProbeResult {
    std::string name;
    bool passed{};
    DWORD error{};
    std::string details;
};

std::string EscapeJson(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (const char value : input) {
        switch (value) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += value;
            break;
        }
    }
    return output;
}

void PrintResult(const ProbeResult& result) {
    std::cout << "{\"probe\":\"" << EscapeJson(result.name) << "\",\"passed\":"
              << (result.passed ? "true" : "false") << ",\"win32_error\":" << result.error
              << ",\"details\":\"" << EscapeJson(result.details) << "\"}\n";
}

#if defined(_MSC_VER)
int InvokeGeneratedFunction(void* address, DWORD& exception_code) {
    __try {
        using GeneratedFunction = int (*)();
        return reinterpret_cast<GeneratedFunction>(address)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exception_code = GetExceptionCode();
        return 0;
    }
}
#endif

ProbeResult ProbeCapabilities() {
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);

    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic_code{};
    const BOOL dynamic_policy_available = GetProcessMitigationPolicy(
        GetCurrentProcess(), ProcessDynamicCodePolicy, &dynamic_code, sizeof(dynamic_code));
    const DWORD policy_error = dynamic_policy_available ? ERROR_SUCCESS : GetLastError();

    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY control_flow_guard{};
    const BOOL cfg_policy_available = GetProcessMitigationPolicy(
        GetCurrentProcess(), ProcessControlFlowGuardPolicy, &control_flow_guard,
        sizeof(control_flow_guard));

    std::ostringstream details;
    details << "architecture=" << system_info.wProcessorArchitecture
            << ";page_size=" << system_info.dwPageSize
            << ";allocation_granularity=" << system_info.dwAllocationGranularity
            << ";dynamic_policy_available=" << (dynamic_policy_available != FALSE)
            << ";dynamic_code_prohibited=" << dynamic_code.ProhibitDynamicCode
            << ";thread_opt_out_allowed=" << dynamic_code.AllowThreadOptOut
            << ";cfg_policy_available=" << (cfg_policy_available != FALSE)
            << ";cfg_enabled=" << control_flow_guard.EnableControlFlowGuard;

    return {
        .name = "capabilities",
        .passed = system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64,
        .error = policy_error,
        .details = details.str(),
    };
}

ProbeResult ProbeVirtualMemory() {
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const SIZE_T size = system_info.dwAllocationGranularity;

    void* allocation =
        VirtualAllocFromApp(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (allocation == nullptr) {
        return {"virtual-memory", false, GetLastError(), "initial reservation failed"};
    }

    std::memset(allocation, 0xA5, size);

    MEMORY_BASIC_INFORMATION memory_info{};
    const SIZE_T queried = VirtualQuery(allocation, &memory_info, sizeof(memory_info));
    if (queried == 0) {
        const DWORD error = GetLastError();
        VirtualFree(allocation, 0, MEM_RELEASE);
        return {"virtual-memory", false, error, "VirtualQuery failed"};
    }

    ULONG old_protection{};
    if (!VirtualProtectFromApp(allocation, size, PAGE_READONLY, &old_protection)) {
        const DWORD error = GetLastError();
        VirtualFree(allocation, 0, MEM_RELEASE);
        return {"virtual-memory", false, error, "RW to RO transition failed"};
    }

    ULONG ignored_protection{};
    if (!VirtualProtectFromApp(allocation, size, PAGE_READWRITE, &ignored_protection)) {
        const DWORD error = GetLastError();
        VirtualFree(allocation, 0, MEM_RELEASE);
        return {"virtual-memory", false, error, "RO to RW transition failed"};
    }

    void* const requested_address = allocation;
    if (!VirtualFree(allocation, 0, MEM_RELEASE)) {
        return {"virtual-memory", false, GetLastError(), "release failed"};
    }

    void* fixed_allocation = VirtualAllocFromApp(requested_address, size,
                                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    const bool fixed_address_succeeded = fixed_allocation == requested_address;
    const DWORD fixed_address_error = fixed_allocation == nullptr ? GetLastError() : ERROR_SUCCESS;
    if (fixed_allocation != nullptr) {
        VirtualFree(fixed_allocation, 0, MEM_RELEASE);
    }

    std::ostringstream details;
    details << "size=" << size << ";state=" << memory_info.State
            << ";type=" << memory_info.Type
            << ";fixed_address_succeeded=" << fixed_address_succeeded;

    return {
        .name = "virtual-memory",
        .passed = fixed_address_succeeded,
        .error = fixed_address_error,
        .details = details.str(),
    };
}

ProbeResult ProbeExecutableMemory() {
#if !defined(_M_X64) && !defined(__x86_64__)
    return {"executable-memory", false, ERROR_NOT_SUPPORTED, "x86-64 is required"};
#else
    constexpr SIZE_T allocation_size = 64 * 1024;
    void* allocation = VirtualAllocFromApp(nullptr, allocation_size,
                                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (allocation == nullptr) {
        return {"executable-memory", false, GetLastError(), "RW allocation failed"};
    }

    // mov eax, 42; ret
    constexpr std::uint8_t program[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    std::memcpy(allocation, program, sizeof(program));

    ULONG old_protection{};
    if (!VirtualProtectFromApp(allocation, allocation_size, PAGE_EXECUTE_READ, &old_protection)) {
        const DWORD error = GetLastError();
        VirtualFree(allocation, 0, MEM_RELEASE);
        return {"executable-memory", false, error, "RW to RX transition failed"};
    }

    if (!FlushInstructionCache(GetCurrentProcess(), allocation, sizeof(program))) {
        const DWORD error = GetLastError();
        VirtualFree(allocation, 0, MEM_RELEASE);
        return {"executable-memory", false, error, "instruction cache flush failed"};
    }

    int return_value{};
    DWORD exception_code{};
#if defined(_MSC_VER)
    return_value = InvokeGeneratedFunction(allocation, exception_code);
#else
    using GeneratedFunction = int (*)();
    return_value = reinterpret_cast<GeneratedFunction>(allocation)();
#endif

    VirtualFree(allocation, 0, MEM_RELEASE);

    std::ostringstream details;
    details << "return_value=" << return_value << ";exception_code=" << exception_code;
    return {
        .name = "executable-memory",
        .passed = exception_code == 0 && return_value == 42,
        .error = exception_code,
        .details = details.str(),
    };
#endif
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view command = argc > 1 ? argv[1] : "--all";
    std::vector<ProbeResult> results;

    if (command == "--all" || command == "--capabilities") {
        results.push_back(ProbeCapabilities());
    }
    if (command == "--all" || command == "--memory") {
        results.push_back(ProbeVirtualMemory());
    }
    if (command == "--all" || command == "--execution") {
        results.push_back(ProbeExecutableMemory());
    }

    if (results.empty()) {
        std::cerr << "usage: xbox_phase0_probe [--all|--capabilities|--memory|--execution]\n";
        return 2;
    }

    bool all_passed = true;
    for (const auto& result : results) {
        PrintResult(result);
        all_passed &= result.passed;
    }
    return all_passed ? 0 : 1;
}
