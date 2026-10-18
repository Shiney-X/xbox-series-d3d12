// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <dxgi1_4.h>
#include <winrt/base.h>
#include "d3d12_mesa_probe.h"

int main() {
    try {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        winrt::check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        D3D12DeviceContext context;
        context.Initialize(warp.Get());
        D3D12ResourceAllocator allocator;
        allocator.Initialize(context.Device(), 64 * 1024 * 1024);
        D3D12PipelineCache cache;
        cache.Initialize(context.Device());
        const auto result = RunD3D12MesaProbe(context, allocator, cache);
        std::cout << result.details << '\n';
        context.WaitForGpu();
        return result.passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
