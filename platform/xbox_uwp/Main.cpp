// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_status_renderer.h"
#include "probes.h"

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Storage;
using namespace Windows::UI::Core;
using XboxSeriesD3D12::Phase0::ProbeResult;

namespace {

fire_and_forget SaveReportAsync(std::string report) {
    try {
        const StorageFolder folder = ApplicationData::Current().LocalFolder();
        const StorageFile file = co_await folder.CreateFileAsync(
            L"phase0-results.jsonl", CreationCollisionOption::ReplaceExisting);
        co_await FileIO::WriteTextAsync(file, to_hstring(report));
    } catch (...) {
        // The status color still communicates the aggregate result if storage fails.
    }
}

class ViewProvider final : public implements<ViewProvider, IFrameworkView> {
public:
    void Initialize(const CoreApplicationView& application_view) {
        application_view.Activated({this, &ViewProvider::OnActivated});
    }

    void SetWindow(const CoreWindow& window) {
        window.Closed([this](const auto&, const auto&) { exit_ = true; });
        results_ = XboxSeriesD3D12::Phase0::RunAllProbes();
        bool passed = XboxSeriesD3D12::Phase0::AllPassed(results_);

        try {
            renderer_ = std::make_unique<D3D12StatusRenderer>();
            const auto bounds = window.Bounds();
            renderer_->Initialize(static_cast<::IUnknown*>(get_abi(window)), bounds.Width,
                                  bounds.Height);
            renderer_->Render(passed);
            results_.push_back({"uwp-presentation", true, 0,
                                "CoreWindow D3D12 swap chain presented"});
        } catch (const hresult_error& error) {
            results_.push_back({"uwp-presentation", false,
                                static_cast<std::uint32_t>(error.code().value),
                                to_string(error.message())});
        }

        SaveReportAsync(XboxSeriesD3D12::Phase0::SerializeJsonLines(results_));
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

class ViewProviderFactory final : public implements<ViewProviderFactory, IFrameworkViewSource> {
public:
    IFrameworkView CreateView() { return make<ViewProvider>(); }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoreApplication::Run(make<ViewProviderFactory>());
    return 0;
}
