// SPDX-License-Identifier: GPL-2.0-or-later

#include <winrt/base.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include "d3d12_status_renderer.h"
#include "memory_pressure_probe.h"
#include "probes.h"

#include <algorithm>
#include <sstream>
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

std::string CreateSessionId() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = now.dwLowDateTime;
    ticks.HighPart = now.dwHighDateTime;

    std::ostringstream output;
    output << ticks.QuadPart << '-' << GetCurrentProcessId();
    return output.str();
}

std::uint32_t AppendLifecycleEvent(const std::string& session_id, const char* event,
                                   const char* details) noexcept {
    try {
        const StorageFolder folder = ApplicationData::Current().LocalFolder();
        const std::wstring path = std::wstring(folder.Path().c_str()) +
                                  L"\\phase0-lifecycle.jsonl";
        const HANDLE file = CreateFile2FromAppW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                                OPEN_ALWAYS, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return GetLastError();
        }

        std::ostringstream line;
        line << "{\"session\":\"" << session_id << "\",\"event\":\"" << event
             << "\",\"details\":\"" << details << "\"}\n";
        const std::string serialized = line.str();
        DWORD written = 0;
        const bool write_succeeded =
            WriteFile(file, serialized.data(), static_cast<DWORD>(serialized.size()), &written,
                      nullptr) != FALSE;
        const bool flush_succeeded = write_succeeded && FlushFileBuffers(file) != FALSE;
        const std::uint32_t error =
            flush_succeeded && written == serialized.size()
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
        CoreApplication::Suspending({this, &ViewProvider::OnSuspending});
        CoreApplication::Resuming({this, &ViewProvider::OnResuming});
    }

    void SetWindow(const CoreWindow& window) {
        window.Closed([this](const auto&, const auto&) { exit_ = true; });
        session_id_ = CreateSessionId();
        results_ = XboxSeriesD3D12::Phase0::RunAllProbes();
        const std::uint32_t journal_error =
            AppendLifecycleEvent(session_id_, "launch", "process started");
        results_.push_back({"lifecycle-journal", journal_error == ERROR_SUCCESS, journal_error,
                            "location=LocalState/phase0-lifecycle.jsonl;session=" + session_id_});
        results_.push_back(ProbeAppMemoryPressure());
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

        PersistReport();
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
    void UpsertResult(ProbeResult result) {
        const auto existing = std::find_if(
            results_.begin(), results_.end(),
            [&result](const ProbeResult& candidate) { return candidate.name == result.name; });
        if (existing == results_.end()) {
            results_.push_back(std::move(result));
        } else {
            *existing = std::move(result);
        }
    }

    void PersistReport() {
        std::erase_if(results_,
                      [](const ProbeResult& result) { return result.name == "report-storage"; });
        results_.push_back(SaveReport(results_));
    }

    void OnActivated(const CoreApplicationView&, const IActivatedEventArgs&) {
        CoreWindow::GetForCurrentThread().Activate();
    }

    void OnSuspending(const IInspectable&, const SuspendingEventArgs& args) noexcept {
        const auto deferral = args.SuspendingOperation().GetDeferral();
        const std::uint32_t journal_error = AppendLifecycleEvent(
            session_id_, "suspend", "suspending event observed;report flushed");
        try {
            if (renderer_) {
                renderer_->Trim();
            }
            UpsertResult({"lifecycle-suspend", journal_error == ERROR_SUCCESS, journal_error,
                          "suspending event observed;DXGI Trim called;session=" + session_id_});
        } catch (const hresult_error& error) {
            UpsertResult({"lifecycle-suspend", false,
                          static_cast<std::uint32_t>(error.code().value),
                          "DXGI Trim failed: " + to_string(error.message())});
        } catch (...) {
            try {
                UpsertResult({"lifecycle-suspend", false, ERROR_GEN_FAILURE,
                              "unknown failure while preparing to suspend"});
            } catch (...) {
                deferral.Complete();
                return;
            }
        }
        try {
            PersistReport();
        } catch (...) {
            // The OS must always receive the completed deferral.
        }
        deferral.Complete();
    }

    void OnResuming(const IInspectable&, const IInspectable&) noexcept {
        const std::uint32_t journal_error =
            AppendLifecycleEvent(session_id_, "resume", "resuming event observed");
        try {
            const bool passed = XboxSeriesD3D12::Phase0::AllPassed(results_);
            if (!renderer_) {
                UpsertResult({"lifecycle-resume", false, ERROR_INVALID_HANDLE,
                              "resuming event observed without an active renderer"});
            } else {
                renderer_->Render(passed);
                UpsertResult({"lifecycle-resume", journal_error == ERROR_SUCCESS, journal_error,
                              "resuming event observed;D3D12 triangle presented again;session=" +
                                  session_id_});
            }
        } catch (const hresult_error& error) {
            UpsertResult({"lifecycle-resume", false,
                          static_cast<std::uint32_t>(error.code().value),
                          to_string(error.message())});
        } catch (...) {
            try {
                UpsertResult({"lifecycle-resume", false, ERROR_GEN_FAILURE,
                              "unknown failure while presenting after resume"});
            } catch (...) {
                return;
            }
        }

        try {
            PersistReport();
        } catch (...) {
            // SaveReport is noexcept; this only guards vector allocation failure.
        }
    }

    bool exit_{};
    std::string session_id_;
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
