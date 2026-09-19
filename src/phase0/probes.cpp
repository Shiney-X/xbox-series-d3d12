// SPDX-License-Identifier: GPL-2.0-or-later

#include "probes.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string_view>
#include <utility>

namespace XboxSeriesD3D12::Phase0 {
namespace {

using Microsoft::WRL::ComPtr;

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

std::string WideToUtf8(std::wstring_view input) {
    if (input.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "<conversion-failed>";
    }

    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(),
                        size, nullptr, nullptr);
    return output;
}

#if defined(_MSC_VER)
int InvokeGeneratedFunction(void* address, bool& exception_caught) {
    __try {
        using GeneratedFunction = int (*)();
        return reinterpret_cast<GeneratedFunction>(address)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        exception_caught = true;
        return 0;
    }
}
#endif

} // namespace

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

    return {"capabilities",
            system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64, policy_error,
            details.str()};
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

    return {"virtual-memory", fixed_address_succeeded, fixed_address_error, details.str()};
}

ProbeResult ProbeMemoryAliases() {
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const SIZE_T view_size = system_info.dwAllocationGranularity;
    const SIZE_T reservation_size = view_size * 2;

    const HANDLE mapping =
        CreateFileMappingFromApp(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, view_size, nullptr);
    if (mapping == nullptr) {
        return {"memory-aliases", false, GetLastError(), "pagefile mapping creation failed"};
    }

    void* const reservation =
        VirtualAlloc2FromApp(GetCurrentProcess(), nullptr, reservation_size,
                             MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
    if (reservation == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(mapping);
        return {"memory-aliases", false, error, "placeholder reservation failed"};
    }

    auto* const second_address = static_cast<std::byte*>(reservation) + view_size;
    if (!VirtualFree(reservation, view_size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        const DWORD error = GetLastError();
        VirtualFree(reservation, 0, MEM_RELEASE);
        CloseHandle(mapping);
        return {"memory-aliases", false, error, "placeholder split failed"};
    }

    void* first_view = nullptr;
    void* second_view = nullptr;
    auto cleanup = [&]() {
        if (first_view != nullptr) {
            UnmapViewOfFileEx(first_view, MEM_PRESERVE_PLACEHOLDER);
        }
        if (second_view != nullptr) {
            UnmapViewOfFileEx(second_view, MEM_PRESERVE_PLACEHOLDER);
        }
        VirtualFree(reservation, 0, MEM_RELEASE);
        VirtualFree(second_address, 0, MEM_RELEASE);
        CloseHandle(mapping);
    };

    first_view = MapViewOfFile3FromApp(mapping, GetCurrentProcess(), reservation, 0, view_size,
                                       MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
    if (first_view != reservation) {
        const DWORD error = GetLastError();
        cleanup();
        return {"memory-aliases", false, error, "first placeholder replacement failed"};
    }

    second_view = MapViewOfFile3FromApp(mapping, GetCurrentProcess(), second_address, 0, view_size,
                                        MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
    if (second_view != second_address) {
        const DWORD error = GetLastError();
        cleanup();
        return {"memory-aliases", false, error, "second placeholder replacement failed"};
    }

    constexpr std::uint64_t first_pattern = 0x0123456789ABCDEFull;
    constexpr std::uint64_t second_pattern = 0xFEDCBA9876543210ull;
    auto* const first_words = static_cast<std::uint64_t*>(first_view);
    auto* const second_words = static_cast<std::uint64_t*>(second_view);

    first_words[0] = first_pattern;
    const bool forward_alias = second_words[0] == first_pattern;
    second_words[1] = second_pattern;
    const bool reverse_alias = first_words[1] == second_pattern;

    cleanup();

    std::ostringstream details;
    details << "view_size=" << view_size << ";reservation_size=" << reservation_size
            << ";fixed_views=1;forward_alias=" << forward_alias
            << ";reverse_alias=" << reverse_alias;
    return {"memory-aliases", forward_alias && reverse_alias,
            forward_alias && reverse_alias ? ERROR_SUCCESS : ERROR_INVALID_DATA, details.str()};
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
    bool exception_caught{};
#if defined(_MSC_VER)
    return_value = InvokeGeneratedFunction(allocation, exception_caught);
#else
    using GeneratedFunction = int (*)();
    return_value = reinterpret_cast<GeneratedFunction>(allocation)();
#endif

    VirtualFree(allocation, 0, MEM_RELEASE);

    std::ostringstream details;
    details << "return_value=" << return_value << ";exception_caught=" << exception_caught;
    return {"executable-memory", !exception_caught && return_value == 42,
            exception_caught ? static_cast<DWORD>(ERROR_FUNCTION_FAILED) : ERROR_SUCCESS,
            details.str()};
#endif
}

ProbeResult ProbeD3D12Device() {
    ComPtr<IDXGIFactory4> factory;
    HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        return {"d3d12-device", false, static_cast<std::uint32_t>(result),
                "CreateDXGIFactory1 failed"};
    }

    ComPtr<IDXGIAdapter1> selected_adapter;
    DXGI_ADAPTER_DESC1 selected_description{};
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        result = factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf());
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            return {"d3d12-device", false, static_cast<std::uint32_t>(result),
                    "EnumAdapters1 failed"};
        }

        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            selected_adapter = std::move(adapter);
            selected_description = description;
            break;
        }
    }

    if (!selected_adapter) {
        return {"d3d12-device", false, static_cast<std::uint32_t>(DXGI_ERROR_UNSUPPORTED),
                "no hardware D3D12 adapter"};
    }

    ComPtr<ID3D12Device> device;
    result = D3D12CreateDevice(selected_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        return {"d3d12-device", false, static_cast<std::uint32_t>(result),
                "D3D12CreateDevice failed"};
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    const HRESULT options_result =
        device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    const HRESULT shader_model_result = device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));

    std::ostringstream details;
    details << "adapter=" << WideToUtf8(selected_description.Description)
            << ";vendor_id=" << selected_description.VendorId
            << ";device_id=" << selected_description.DeviceId
            << ";dedicated_video_memory=" << selected_description.DedicatedVideoMemory
            << ";node_count=" << device->GetNodeCount()
            << ";options_available=" << SUCCEEDED(options_result)
            << ";resource_binding_tier="
            << (SUCCEEDED(options_result) ? static_cast<unsigned>(options.ResourceBindingTier) : 0)
            << ";shader_model_available=" << SUCCEEDED(shader_model_result)
            << ";highest_shader_model="
            << (SUCCEEDED(shader_model_result) ? static_cast<unsigned>(shader_model.HighestShaderModel)
                                               : 0);

    return {"d3d12-device", true, ERROR_SUCCESS, details.str()};
}

std::vector<ProbeResult> RunAllProbes() {
    std::vector<ProbeResult> results;
    results.reserve(5);
    results.push_back(ProbeCapabilities());
    results.push_back(ProbeVirtualMemory());
    results.push_back(ProbeMemoryAliases());
    results.push_back(ProbeExecutableMemory());
    results.push_back(ProbeD3D12Device());
    return results;
}

bool AllPassed(const std::vector<ProbeResult>& results) noexcept {
    return !results.empty() &&
           std::all_of(results.begin(), results.end(),
                       [](const ProbeResult& result) { return result.passed; });
}

std::string SerializeJsonLine(const ProbeResult& result) {
    std::ostringstream output;
    output << "{\"probe\":\"" << EscapeJson(result.name) << "\",\"passed\":"
           << (result.passed ? "true" : "false") << ",\"win32_error\":" << result.error
           << ",\"details\":\"" << EscapeJson(result.details) << "\"}";
    return output.str();
}

std::string SerializeJsonLines(const std::vector<ProbeResult>& results) {
    std::ostringstream output;
    for (const auto& result : results) {
        output << SerializeJsonLine(result) << '\n';
    }
    return output.str();
}

} // namespace XboxSeriesD3D12::Phase0
