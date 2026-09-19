// SPDX-License-Identifier: GPL-2.0-or-later

#include <winrt/base.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include "d3d12_status_renderer.h"
#include "probes.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::UI::Core;
using XboxSeriesD3D12::Phase0::ProbeResult;

namespace {

std::uint32_t WriteReport(const StorageFolder& folder, const wchar_t* filename,
                          const std::vector<ProbeResult>& results,
                          const ProbeResult& storage_result) noexcept {
    try {
        std::vector<ProbeResult> persisted_results = results;
        persisted_results.push_back(storage_result);
        const std::string report =
            XboxSeriesD3D12::Phase0::SerializeJsonLines(persisted_results);
        const hstring folder_path = folder.Path();
        const std::wstring path = std::wstring(folder_path.c_str()) + L"\\" + filename;

        const HANDLE file = CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                CREATE_ALWAYS, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return GetLastError();
        }

        DWORD written = 0;
        const bool write_succeeded =
            WriteFile(file, report.data(), static_cast<DWORD>(report.size()), &written,
                      nullptr) != FALSE;
        const std::uint32_t error = write_succeeded && written == report.size()
                                        ? ERROR_SUCCESS
                                        : (write_succeeded ? ERROR_WRITE_FAULT : GetLastError());
        CloseHandle(file);
        return error;
    } catch (const hresult_error& error) {
        return static_cast<std::uint32_t>(error.code().value);
    } catch (...) {
        return ERROR_GEN_FAILURE;
    }
}

ProbeResult SaveReport(const std::vector<ProbeResult>& results) noexcept {
    constexpr wchar_t filename[] = L"phase0-results.jsonl";

    ProbeResult local_state{"report-storage", true, ERROR_SUCCESS,
                            "location=LocalState/phase0-results.jsonl;mode=synchronous"};
    std::uint32_t local_state_error = ERROR_GEN_FAILURE;
    try {
        local_state_error =
            WriteReport(ApplicationData::Current().LocalFolder(), filename, results, local_state);
    } catch (const hresult_error& error) {
        local_state_error = static_cast<std::uint32_t>(error.code().value);
    } catch (...) {
        local_state_error = ERROR_GEN_FAILURE;
    }
    if (local_state_error == ERROR_SUCCESS) {
        return local_state;
    }

    ProbeResult local_cache{
        "report-storage", true, ERROR_SUCCESS,
        "location=LocalCache/phase0-results.jsonl;mode=synchronous;local_state_error=" +
            std::to_string(local_state_error)};
    std::uint32_t local_cache_error = ERROR_GEN_FAILURE;
    try {
        local_cache_error = WriteReport(ApplicationData::Current().LocalCacheFolder(), filename,
                                        results, local_cache);
    } catch (const hresult_error& error) {
        local_cache_error = static_cast<std::uint32_t>(error.code().value);
    } catch (...) {
        local_cache_error = ERROR_GEN_FAILURE;
    }
    if (local_cache_error == ERROR_SUCCESS) {
        return local_cache;
    }

    return {"report-storage", false, local_state_error,
            "location=unavailable;mode=synchronous;local_state_error=" +
                std::to_string(local_state_error) +
                ";local_cache_error=" + std::to_string(local_cache_error)};
}

class ViewProvider : public implements<ViewProvider, IFrameworkView> {
public:
    void Initialize(const CoreApplicationView& application_view) {
        application_view.Activated({this, &ViewProvider::OnActivated});
    }

    void SetWindow(const CoreWindow& window) {
        window.Closed([this](const auto&, const auto&) { exit_ = true; });
        results_ = XboxSeriesD3D12::Phase0::RunAllProbes();
        bool passed = XboxSeriesD3D12::Phase0::AllPassed(results_);
        bool presentation_succeeded = false;

        try {
            renderer_ = std::make_unique<D3D12StatusRenderer>();
            const auto bounds = window.Bounds();
            renderer_->Initialize(static_cast<::IUnknown*>(get_abi(window)), bounds.Width,
                                  bounds.Height);
            renderer_->Render(passed);
            results_.push_back({"uwp-presentation", true, 0,
                                "CoreWindow D3D12 triangle presented;shader_compiler=DXC;"
                                "dxc_api=IDxcCompiler;shader_model=6_0;shader_format=DXIL;"
                                "draw_vertices=3"});
            presentation_succeeded = true;
        } catch (const hresult_error& error) {
            results_.push_back({"uwp-presentation", false,
                                static_cast<std::uint32_t>(error.code().value),
                                to_string(error.message())});
            if (renderer_) {
                try {
                    renderer_->Render(false);
                } catch (...) {
                    // The JSON report remains the authoritative failure channel.
                }
            }
        }

        results_.push_back(SaveReport(results_));
        passed = XboxSeriesD3D12::Phase0::AllPassed(results_);
        if (presentation_succeeded) {
            renderer_->Render(passed);
        }
    }

    void Load(const hstring&) {}

    void Run() {
        const CoreDispatcher dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
        while (!exit_) {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
    }

    void Uninitialize() { renderer_.reset(); }

private:
    void OnActivated(const CoreApplicationView&, const IActivatedEventArgs&) {
        CoreWindow::GetForCurrentThread().Activate();
    }

    bool exit_{};
    std::vector<ProbeResult> results_;
    std::unique_ptr<D3D12StatusRenderer> renderer_;
};

class ViewProviderFactory : public implements<ViewProviderFactory, IFrameworkViewSource> {
public:
    IFrameworkView CreateView() { return make<ViewProvider>(); }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoreApplication::Run(make<ViewProviderFactory>());
    return 0;
}
