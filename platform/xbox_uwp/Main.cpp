// SPDX-License-Identifier: GPL-2.0-or-later

#include <winrt/base.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include "core/uwp/core_bridge.h"
#include "core/uwp/guest_data_link_fixture.h"
#include "core/uwp/guest_import_fixture.h"
#include "core/uwp/guest_link_fixture.h"
#include "core/uwp/guest_loader_fixture.h"
#include "core/uwp/guest_payload_fixture.h"
#include "core/uwp/guest_preflight.h"
#include "d3d12_status_renderer.h"
#include "guest_execution_probe.h"
#include "guest_startup_session.h"
#include "library_folder_access.h"
#include "memory_pressure_probe.h"
#include "probes.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::System;
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

std::uint32_t AppendLifecycleEvent(const std::string &session_id,
                                   const char *event,
                                   const char *details) noexcept {
  try {
    const StorageFolder folder = ApplicationData::Current().LocalFolder();
    const std::wstring path =
        std::wstring(folder.Path().c_str()) + L"\\phase0-lifecycle.jsonl";
    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    std::ostringstream line;
    line << "{\"session\":\"" << session_id << "\",\"event\":\"" << event
         << "\",\"details\":\"" << details << "\"}\n";
    const std::string serialized = line.str();
    DWORD written = 0;
    const bool write_succeeded =
        WriteFile(file, serialized.data(),
                  static_cast<DWORD>(serialized.size()), &written,
                  nullptr) != FALSE;
    const bool flush_succeeded =
        write_succeeded && FlushFileBuffers(file) != FALSE;
    const std::uint32_t error =
        flush_succeeded && written == serialized.size()
            ? ERROR_SUCCESS
            : (write_succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

std::uint32_t WriteReport(const StorageFolder &folder, const wchar_t *filename,
                          const std::vector<ProbeResult> &results,
                          const ProbeResult &storage_result) noexcept {
  try {
    std::vector<ProbeResult> persisted_results = results;
    persisted_results.push_back(storage_result);
    const std::string report =
        XboxSeriesD3D12::Phase0::SerializeJsonLines(persisted_results);
    const hstring folder_path = folder.Path();
    const std::wstring path =
        std::wstring(folder_path.c_str()) + L"\\" + filename;

    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    DWORD written = 0;
    const bool write_succeeded =
        WriteFile(file, report.data(), static_cast<DWORD>(report.size()),
                  &written, nullptr) != FALSE;
    const std::uint32_t error =
        write_succeeded && written == report.size()
            ? ERROR_SUCCESS
            : (write_succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

ProbeResult SaveReport(const std::vector<ProbeResult> &results) noexcept {
  constexpr wchar_t filename[] = L"phase0-results.jsonl";

  ProbeResult local_state{
      "report-storage", true, ERROR_SUCCESS,
      "location=LocalState/phase0-results.jsonl;mode=synchronous"};
  std::uint32_t local_state_error = ERROR_GEN_FAILURE;
  try {
    local_state_error = WriteReport(ApplicationData::Current().LocalFolder(),
                                    filename, results, local_state);
  } catch (const hresult_error &error) {
    local_state_error = static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    local_state_error = ERROR_GEN_FAILURE;
  }
  if (local_state_error == ERROR_SUCCESS) {
    return local_state;
  }

  ProbeResult local_cache{
      "report-storage", true, ERROR_SUCCESS,
      "location=LocalCache/"
      "phase0-results.jsonl;mode=synchronous;local_state_error=" +
          std::to_string(local_state_error)};
  std::uint32_t local_cache_error = ERROR_GEN_FAILURE;
  try {
    local_cache_error =
        WriteReport(ApplicationData::Current().LocalCacheFolder(), filename,
                    results, local_cache);
  } catch (const hresult_error &error) {
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

std::uint32_t
WriteCoreBridgeReport(const ::Core::Uwp::BridgeStatus &status) noexcept {
  try {
    const StorageFolder folder = ApplicationData::Current().LocalFolder();
    const std::wstring path =
        std::wstring(folder.Path().c_str()) + L"\\phase1-core.jsonl";
    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    std::ostringstream output;
    output << "{\"component\":\"shadps4-core-uwp\",\"passed\":"
           << (status.AllPassed() ? "true" : "false") << ",\"details\":\""
           << status.SerializeDetails() << "\"}\n";
    const std::string serialized = output.str();
    DWORD written = 0;
    const bool succeeded = WriteFile(file, serialized.data(),
                                     static_cast<DWORD>(serialized.size()),
                                     &written, nullptr) != FALSE;
    const std::uint32_t error =
        succeeded && written == serialized.size()
            ? ERROR_SUCCESS
            : (succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

std::string EscapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped += character < 0x20U ? '?' : static_cast<char>(character);
      break;
    }
  }
  return escaped;
}

std::string CreateFolderLabel(std::string_view name) {
  std::string label;
  label.reserve(std::min<std::size_t>(name.size(), 24U));
  bool previous_was_space = false;
  for (const unsigned char character : name) {
    if (label.size() >= 24U) {
      break;
    }
    if (std::isalnum(character) != 0 || character == '-' || character == '.') {
      label.push_back(static_cast<char>(std::toupper(character)));
      previous_was_space = false;
    } else if (!previous_was_space && !label.empty()) {
      label.push_back(' ');
      previous_was_space = true;
    }
  }
  while (!label.empty() && label.back() == ' ') {
    label.pop_back();
  }
  return label.empty() ? "SELECTED FOLDER" : label;
}

std::uint32_t WriteLibraryReport(bool passed, std::string_view state,
                                 std::uint32_t operation_error,
                                 std::string_view folder_name,
                                 std::string_view details) noexcept {
  try {
    const StorageFolder folder = ApplicationData::Current().LocalFolder();
    const std::wstring path =
        std::wstring(folder.Path().c_str()) + L"\\phase1-library.jsonl";
    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    std::ostringstream output;
    output << "{\"component\":\"uwp-library-folder\",\"passed\":"
           << (passed ? "true" : "false") << ",\"state\":\""
           << EscapeJson(state) << "\",\"win32_error\":" << operation_error
           << ",\"folder_name\":\"" << EscapeJson(folder_name)
           << "\",\"details\":\"" << EscapeJson(details) << "\"}\n";
    const std::string serialized = output.str();
    DWORD written = 0;
    const bool succeeded = WriteFile(file, serialized.data(),
                                     static_cast<DWORD>(serialized.size()),
                                     &written, nullptr) != FALSE;
    const bool flushed = succeeded && FlushFileBuffers(file) != FALSE;
    const std::uint32_t error =
        flushed && written == serialized.size()
            ? ERROR_SUCCESS
            : (succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

struct DiscoveredGame {
  XboxGameListEntry display;
  std::string folder_name;
  StorageFile executable{nullptr};
};

std::string_view IconStateName(GameIconState state) noexcept {
  switch (state) {
  case GameIconState::Ready:
    return "ready";
  case GameIconState::Invalid:
    return "invalid";
  case GameIconState::Missing:
  default:
    return "missing";
  }
}

std::uint64_t HashIcon(const std::vector<std::uint8_t> &pixels,
                       std::uint32_t width, std::uint32_t height) noexcept {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  std::uint64_t hash = offset_basis;
  for (const std::uint8_t value : pixels) {
    hash = (hash ^ value) * prime;
  }
  hash = (hash ^ width) * prime;
  hash = (hash ^ height) * prime;
  return hash == 0U ? 1U : hash;
}

std::uint32_t WriteLibraryScanReport(
    bool passed, std::string_view state, std::uint32_t operation_error,
    std::size_t directories_scanned, std::size_t invalid_metadata,
    const std::vector<DiscoveredGame> &games) noexcept {
  try {
    const StorageFolder folder = ApplicationData::Current().LocalFolder();
    const std::wstring path =
        std::wstring(folder.Path().c_str()) + L"\\phase1-library-scan.jsonl";
    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    std::ostringstream output;
    output << "{\"component\":\"uwp-library-scan\",\"passed\":"
           << (passed ? "true" : "false") << ",\"state\":\""
           << EscapeJson(state) << "\",\"win32_error\":" << operation_error
           << ",\"directories_scanned\":" << directories_scanned
           << ",\"invalid_metadata\":" << invalid_metadata
           << ",\"games_found\":" << games.size() << "}\n";
    for (std::size_t index = 0; index < games.size(); ++index) {
      const DiscoveredGame &game = games[index];
      output << "{\"component\":\"uwp-library-game\",\"index\":" << index
             << ",\"title\":\"" << EscapeJson(game.display.title)
             << "\",\"title_id\":\"" << EscapeJson(game.display.title_id)
             << "\",\"app_version\":\"" << EscapeJson(game.display.app_version)
             << "\",\"folder_name\":\"" << EscapeJson(game.folder_name)
             << "\"}\n";
    }
    const std::string serialized = output.str();
    DWORD written = 0;
    const bool succeeded = WriteFile(file, serialized.data(),
                                     static_cast<DWORD>(serialized.size()),
                                     &written, nullptr) != FALSE;
    const bool flushed = succeeded && FlushFileBuffers(file) != FALSE;
    const std::uint32_t error =
        flushed && written == serialized.size()
            ? ERROR_SUCCESS
            : (succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

std::uint32_t WriteLibraryIconReport(bool passed, std::string_view state,
                                     std::uint32_t operation_error,
                                     const std::vector<DiscoveredGame> &games,
                                     bool selected_icon_presented) noexcept {
  try {
    const StorageFolder folder = ApplicationData::Current().LocalFolder();
    const std::wstring path =
        std::wstring(folder.Path().c_str()) + L"\\phase1-library-icons.jsonl";
    const HANDLE file = CreateFile2FromAppW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return GetLastError();
    }

    const auto count_state = [&games](GameIconState state_value) {
      return std::count_if(games.begin(), games.end(),
                           [state_value](const DiscoveredGame &game) {
                             return game.display.icon_state == state_value;
                           });
    };
    const std::size_t ready = count_state(GameIconState::Ready);
    const std::size_t missing = count_state(GameIconState::Missing);
    const std::size_t invalid = count_state(GameIconState::Invalid);

    std::ostringstream output;
    output << "{\"component\":\"uwp-library-icons\",\"passed\":"
           << (passed ? "true" : "false") << ",\"state\":\""
           << EscapeJson(state) << "\",\"win32_error\":" << operation_error
           << ",\"icons_ready\":" << ready << ",\"icons_missing\":" << missing
           << ",\"icons_invalid\":" << invalid
           << ",\"selected_icon_presented\":"
           << (selected_icon_presented ? "true" : "false") << "}\n";
    for (std::size_t index = 0; index < games.size(); ++index) {
      const XboxGameListEntry &game = games[index].display;
      output << "{\"component\":\"uwp-library-icon\",\"index\":" << index
             << ",\"title_id\":\"" << EscapeJson(game.title_id)
             << "\",\"state\":\"" << IconStateName(game.icon_state)
             << "\",\"width\":" << game.icon_width
             << ",\"height\":" << game.icon_height
             << ",\"win32_error\":" << game.icon_error << "}\n";
    }

    const std::string serialized = output.str();
    DWORD written = 0;
    const bool succeeded = WriteFile(file, serialized.data(),
                                     static_cast<DWORD>(serialized.size()),
                                     &written, nullptr) != FALSE;
    const bool flushed = succeeded && FlushFileBuffers(file) != FALSE;
    const std::uint32_t error =
        flushed && written == serialized.size()
            ? ERROR_SUCCESS
            : (succeeded ? ERROR_WRITE_FAULT : GetLastError());
    CloseHandle(file);
    return error;
  } catch (const hresult_error &error) {
    return static_cast<std::uint32_t>(error.code().value);
  } catch (...) {
    return ERROR_GEN_FAILURE;
  }
}

class ViewProvider : public implements<ViewProvider, IFrameworkView> {
public:
  void Initialize(const CoreApplicationView &application_view) {
    application_view.Activated({this, &ViewProvider::OnActivated});
    CoreApplication::Suspending({this, &ViewProvider::OnSuspending});
    CoreApplication::Resuming({this, &ViewProvider::OnResuming});
  }

  void SetWindow(const CoreWindow &window) {
    window.Closed([this](const auto &, const auto &) { exit_ = true; });
    window.KeyDown({this, &ViewProvider::OnKeyDown});
    session_id_ = CreateSessionId();
    bridge_status_ = ::Core::Uwp::InitializeBridge();
    results_ = XboxSeriesD3D12::Phase0::RunAllProbes();
    ProbeResult loader_result{
        "guest-loader-fixture", false, ERROR_INVALID_DATA,
        "stage=data_staging;guest_executed=0;game_frame=0"};
    try {
      const auto fixture = ::Core::Uwp::MakeGuestLoaderFixture();
      const auto image = ::Core::Uwp::StageRawGuest(fixture);
      loader_result.passed = ::Core::Uwp::VerifyGuestLoaderFixture(image);
      const auto self_image = ::Core::Uwp::StageGuestPayload(
          ::Core::Uwp::MakeGuestPayloadFixture());
      const bool self_fixture_verified =
          self_image.Ready() &&
          ::Core::Uwp::VerifyGuestLoaderFixture(self_image.image);
      const bool link_fixture_verified = ::Core::Uwp::VerifyGuestLinkFixture(
          ::Core::Uwp::InspectGuestLinkManifest(
              ::Core::Uwp::MakeGuestLinkFixture()));
      const bool relocation_fixture_verified =
          ::Core::Uwp::VerifyGuestDataLinkFixtures();
      const bool import_fixture_verified =
          ::Core::Uwp::VerifyGuestImportFixtures();
      loader_result.passed = loader_result.passed && self_fixture_verified &&
                             link_fixture_verified &&
                             relocation_fixture_verified &&
                             import_fixture_verified;
      loader_result.error = static_cast<std::uint32_t>(
          loader_result.passed ? ERROR_SUCCESS : ERROR_INVALID_DATA);
      loader_result.details =
          image.plan.Details() + ";fixture=authored;staged_bytes=" +
          std::to_string(image.bytes.size()) +
          ";copy_bss_verified=" + std::to_string(loader_result.passed) +
          ";stage_error=" + image.error + ";self_data_fixture_verified=" +
          std::to_string(self_fixture_verified) +
          ";link_manifest_fixture_verified=" +
          std::to_string(link_fixture_verified) +
          ";data_relocation_fixture_verified=" +
          std::to_string(relocation_fixture_verified) +
          ";import_resolver_fixture_verified=" +
          std::to_string(import_fixture_verified);
    } catch (const std::exception &) {
      loader_result.details += ";error=fixture_allocation_or_staging_failed";
    }
    const auto loader_report_error =
        WriteReport(ApplicationData::Current().LocalFolder(),
                    L"phase5-loader.jsonl", {loader_result},
                    {"guest-loader-report", true, ERROR_SUCCESS,
                     "guest_executed=0;game_frame=0"});
    results_.push_back(loader_result);
    results_.push_back(
        {"guest-loader-storage", loader_report_error == ERROR_SUCCESS,
         loader_report_error, "report=LocalState/phase5-loader.jsonl"});
    const auto execution_result = ProbeGuestExecution();
    const auto execution_report_error =
        WriteReport(ApplicationData::Current().LocalFolder(),
                    L"phase5-execution.jsonl", {execution_result},
                    {"guest-execution-report", true, ERROR_SUCCESS,
                     "game_executed=0;game_frame=0"});
    results_.push_back(execution_result);
    results_.push_back(
        {"guest-execution-storage", execution_report_error == ERROR_SUCCESS,
         execution_report_error, "report=LocalState/phase5-execution.jsonl"});
    const std::uint32_t bridge_report_error =
        WriteCoreBridgeReport(bridge_status_);
    results_.push_back(
        {"shadps4-core-uwp",
         bridge_status_.AllPassed() && bridge_report_error == ERROR_SUCCESS,
         bridge_report_error,
         bridge_status_.SerializeDetails() +
             ";report=LocalState/phase1-core.jsonl"});
    const std::uint32_t journal_error =
        AppendLifecycleEvent(session_id_, "launch", "process started");
    results_.push_back(
        {"lifecycle-journal", journal_error == ERROR_SUCCESS, journal_error,
         "location=LocalState/phase0-lifecycle.jsonl;session=" + session_id_});
    results_.push_back(ProbeAppMemoryPressure());
    bool passed = XboxSeriesD3D12::Phase0::AllPassed(results_);
    bool presentation_succeeded = false;

    try {
      renderer_ = std::make_unique<D3D12StatusRenderer>();
      const auto bounds = window.Bounds();
      renderer_->Initialize(static_cast<::IUnknown *>(get_abi(window)),
                            bounds.Width, bounds.Height);
      shell_state_.core_ready = bridge_status_.AllPassed();
      shell_state_.probes_passed = passed;
      shell_state_.upstream_version = bridge_status_.upstream_version;
      renderer_->Render(shell_state_);
      results_.push_back(
          {"uwp-presentation", true, 0,
           "CoreWindow D3D12 Xbox Shell presented;shader_compiler=DXC;"
           "dxc_api=IDxcCompiler;shader_model=6_0;shader_format=DXIL;"
           "input=CoreWindow.Gamepad;core_bridge=linked"});
      presentation_succeeded = true;
    } catch (const hresult_error &error) {
      results_.push_back({"uwp-presentation", false,
                          static_cast<std::uint32_t>(error.code().value),
                          to_string(error.message())});
    }

    PersistReport();
    passed = XboxSeriesD3D12::Phase0::AllPassed(results_);
    if (presentation_succeeded) {
      shell_state_.probes_passed = passed;
      renderer_->Render(shell_state_);
      DetectRemovableStorage();
    }
  }

  void Load(const hstring &) {}

  void Run() {
    const CoreDispatcher dispatcher =
        CoreWindow::GetForCurrentThread().Dispatcher();
    while (!exit_) {
      dispatcher.ProcessEvents(
          CoreProcessEventsOption::ProcessOneAndAllPending);
    }
  }

  void Uninitialize() { renderer_.reset(); }

private:
  void PresentLibraryState(bool passed, std::string_view report_state,
                           std::uint32_t error, std::string_view folder_name,
                           std::string_view details) noexcept {
    const std::uint32_t report_error =
        WriteLibraryReport(passed, report_state, error, folder_name, details);
    if (report_error != ERROR_SUCCESS) {
      shell_state_.library_folder_state = LibraryFolderState::Failed;
      shell_state_.library_folder_name.clear();
    }
    try {
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
    } catch (...) {
      shell_state_.library_folder_state = LibraryFolderState::Failed;
    }
  }

  std::string CreateBreadcrumb(const StorageFolder &folder) const {
    std::string breadcrumb;
    for (const StorageFolder &parent : folder_stack_) {
      if (!breadcrumb.empty()) {
        breadcrumb += " / ";
      }
      breadcrumb += CreateFolderLabel(to_string(parent.Name()));
    }
    if (!breadcrumb.empty()) {
      breadcrumb += " / ";
    }
    breadcrumb += CreateFolderLabel(to_string(folder.Name()));
    constexpr std::size_t maximum_length = 52U;
    if (breadcrumb.size() > maximum_length) {
      breadcrumb = "... / " + breadcrumb.substr(breadcrumb.size() - 46U);
    }
    return breadcrumb;
  }

  std::string CreateRelativeLibraryPath() const {
    if (folder_stack_.empty()) {
      return {};
    }
    std::string relative_path;
    for (std::size_t index = 1U; index < folder_stack_.size(); ++index) {
      if (!relative_path.empty()) {
        relative_path += '/';
      }
      relative_path += to_string(folder_stack_[index].Name());
    }
    if (!relative_path.empty()) {
      relative_path += '/';
    }
    relative_path += to_string(current_folder_.Name());
    return relative_path;
  }

  void PersistLibrarySelection() {
    const auto values = ApplicationData::Current().LocalSettings().Values();
    values.Insert(hstring{L"libraryRelativePath"},
                  box_value(to_hstring(CreateRelativeLibraryPath())));
  }

  std::optional<std::vector<std::string>> ReadPersistedLibraryPath() const {
    try {
      const auto values = ApplicationData::Current().LocalSettings().Values();
      const hstring key{L"libraryRelativePath"};
      if (!values.HasKey(key)) {
        return std::nullopt;
      }
      const std::string stored =
          to_string(unbox_value<hstring>(values.Lookup(key)));
      constexpr std::size_t maximum_stored_path = 2048U;
      constexpr std::size_t maximum_components = 16U;
      if (stored.size() > maximum_stored_path) {
        return std::nullopt;
      }
      std::vector<std::string> components;
      std::size_t begin = 0U;
      while (begin < stored.size()) {
        const std::size_t separator = stored.find('/', begin);
        const std::size_t end =
            separator == std::string::npos ? stored.size() : separator;
        if (end > begin) {
          if (components.size() >= maximum_components) {
            return std::nullopt;
          }
          components.push_back(stored.substr(begin, end - begin));
        }
        if (separator == std::string::npos) {
          break;
        }
        begin = separator + 1U;
      }
      return components;
    } catch (...) {
      return std::nullopt;
    }
  }

  void SetBrowserReady(
      const StorageFolder &folder,
      const winrt::Windows::Foundation::Collections::IVectorView<StorageFolder>
          &subfolders,
      std::string_view report_state) {
    const std::string folder_name = to_string(folder.Name());
    current_folder_ = folder;
    child_folders_.clear();
    shell_state_.library_entries.clear();
    child_folders_.reserve(subfolders.Size());
    shell_state_.library_entries.reserve(subfolders.Size());
    for (const StorageFolder &subfolder : subfolders) {
      child_folders_.push_back(subfolder);
      shell_state_.library_entries.push_back(
          CreateFolderLabel(to_string(subfolder.Name())));
    }
    shell_state_.library_folder_state = LibraryFolderState::Ready;
    shell_state_.library_folder_name = CreateFolderLabel(folder_name);
    shell_state_.library_breadcrumb = CreateBreadcrumb(folder);
    shell_state_.selected_library_entry = 0U;
    shell_state_.library_at_device_root = folder_stack_.empty();
    shell_state_.library_selection_confirmed = false;
    shell_state_.library_scan_state = LibraryScanState::Inactive;
    shell_state_.games.clear();
    shell_state_.selected_game = 0U;
    std::ostringstream details;
    details << "source=KnownFolders.RemovableDevices;device_index=0;depth="
            << folder_stack_.size() << ";subfolders=" << child_folders_.size();
    PresentLibraryState(true, report_state, ERROR_SUCCESS, folder_name,
                        details.str());
    AppendLifecycleEvent(session_id_, "library-folder",
                         "USB folder enumerated;input_consumed=1");
  }

  void SetLibraryFailure(const hresult_error &error,
                         std::string_view operation) noexcept {
    library_operation_active_ = false;
    shell_state_.library_folder_state = LibraryFolderState::Failed;
    shell_state_.library_entries.clear();
    shell_state_.library_selection_confirmed = false;
    PresentLibraryState(false, "usb_browser_failed",
                        static_cast<std::uint32_t>(error.code().value), {},
                        std::string(operation) + ": " +
                            to_string(error.message()));
  }

  void SetUnknownLibraryFailure(std::string_view operation) noexcept {
    library_operation_active_ = false;
    shell_state_.library_folder_state = LibraryFolderState::Failed;
    shell_state_.library_entries.clear();
    shell_state_.library_selection_confirmed = false;
    PresentLibraryState(false, "usb_browser_failed", ERROR_GEN_FAILURE, {},
                        std::string(operation) + ": unknown failure");
  }

  fire_and_forget DetectRemovableStorage() {
    [[maybe_unused]] const auto lifetime = get_strong();
    if (library_operation_active_) {
      co_return;
    }

    try {
      library_operation_active_ = true;
      shell_state_.library_folder_state = LibraryFolderState::Restoring;
      shell_state_.library_folder_name.clear();
      shell_state_.library_breadcrumb.clear();
      shell_state_.library_entries.clear();
      shell_state_.library_selection_confirmed = false;
      if (renderer_) {
        renderer_->Render(shell_state_);
      }

      const StorageFolder folder =
          co_await library_folder_access_.FindFirstRemovableDeviceAsync();
      if (!folder) {
        library_operation_active_ = false;
        current_folder_ = nullptr;
        child_folders_.clear();
        folder_stack_.clear();
        shell_state_.library_folder_state = LibraryFolderState::NotConfigured;
        shell_state_.library_folder_name.clear();
        PresentLibraryState(true, "usb_not_found", ERROR_SUCCESS, {},
                            "source=KnownFolders.RemovableDevices;devices=0");
        co_return;
      }

      StorageFolder selected_folder = folder;
      folder_stack_.clear();
      const auto persisted_path = ReadPersistedLibraryPath();
      bool restored_selection = persisted_path.has_value();
      if (persisted_path) {
        for (const std::string &component : *persisted_path) {
          const IStorageItem item =
              co_await selected_folder.TryGetItemAsync(to_hstring(component));
          const StorageFolder child =
              item ? item.try_as<StorageFolder>() : nullptr;
          if (!child) {
            restored_selection = false;
            break;
          }
          folder_stack_.push_back(selected_folder);
          selected_folder = child;
        }
      }
      if (!restored_selection) {
        folder_stack_.clear();
        selected_folder = folder;
        ApplicationData::Current().LocalSettings().Values().Remove(
            hstring{L"libraryRelativePath"});
      }
      const auto subfolders =
          co_await library_folder_access_.GetSubfoldersAsync(selected_folder);
      library_operation_active_ = false;
      SetBrowserReady(selected_folder, subfolders,
                      restored_selection ? "library_folder_restored"
                                         : "usb_browser_ready");
      if (restored_selection) {
        shell_state_.library_selection_confirmed = true;
        ScanSelectedLibrary();
      }
    } catch (const hresult_error &error) {
      SetLibraryFailure(error, "scan removable storage");
    } catch (...) {
      SetUnknownLibraryFailure("scan removable storage");
    }
  }

  fire_and_forget OpenSelectedFolder() {
    [[maybe_unused]] const auto lifetime = get_strong();
    if (library_operation_active_ ||
        shell_state_.selected_library_entry >= child_folders_.size()) {
      co_return;
    }

    const StorageFolder target =
        child_folders_[shell_state_.selected_library_entry];
    try {
      library_operation_active_ = true;
      shell_state_.library_folder_state = LibraryFolderState::Restoring;
      shell_state_.library_selection_confirmed = false;
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
      const auto subfolders =
          co_await library_folder_access_.GetSubfoldersAsync(target);
      folder_stack_.push_back(current_folder_);
      library_operation_active_ = false;
      SetBrowserReady(target, subfolders, "usb_folder_opened");
    } catch (const hresult_error &error) {
      SetLibraryFailure(error, "open USB folder");
    } catch (...) {
      SetUnknownLibraryFailure("open USB folder");
    }
  }

  fire_and_forget OpenParentFolder() {
    [[maybe_unused]] const auto lifetime = get_strong();
    if (library_operation_active_ || folder_stack_.empty()) {
      co_return;
    }

    const StorageFolder target = folder_stack_.back();
    try {
      library_operation_active_ = true;
      shell_state_.library_folder_state = LibraryFolderState::Restoring;
      shell_state_.library_selection_confirmed = false;
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
      const auto subfolders =
          co_await library_folder_access_.GetSubfoldersAsync(target);
      folder_stack_.pop_back();
      library_operation_active_ = false;
      SetBrowserReady(target, subfolders, "usb_folder_parent");
    } catch (const hresult_error &error) {
      SetLibraryFailure(error, "open parent USB folder");
    } catch (...) {
      SetUnknownLibraryFailure("open parent USB folder");
    }
  }

  fire_and_forget ScanSelectedLibrary() {
    [[maybe_unused]] const auto lifetime = get_strong();
    if (library_operation_active_ || !current_folder_) {
      co_return;
    }

    struct PendingFolder {
      StorageFolder folder{nullptr};
      std::uint32_t depth{};
    };

    constexpr std::uint32_t maximum_depth = 3U;
    constexpr std::size_t maximum_directories = 128U;
    constexpr std::size_t maximum_games = 32U;
    std::vector<PendingFolder> pending{{current_folder_, 0U}};
    std::vector<DiscoveredGame> discovered;
    std::vector<ProbeResult> guest_preflight_results;
    std::vector<ProbeResult> guest_segment_results;
    std::vector<ProbeResult> guest_payload_results;
    std::vector<ProbeResult> guest_link_results;
    std::vector<ProbeResult> guest_relocation_results;
    std::vector<ProbeResult> guest_import_results;
    constexpr std::uint64_t maximum_payload_scan_bytes = 64ULL * 1024 * 1024;
    constexpr std::size_t maximum_payload_files = 8;
    std::uint64_t payload_bytes_requested = 0;
    std::size_t payload_files_attempted = 0;
    std::size_t next_folder = 0U;
    std::size_t directories_scanned = 0U;
    std::size_t invalid_metadata = 0U;

    try {
      library_operation_active_ = true;
      shell_state_.library_scan_state = LibraryScanState::Scanning;
      shell_state_.games.clear();
      shell_state_.selected_game = 0U;
      if (renderer_) {
        renderer_->Render(shell_state_);
      }

      while (next_folder < pending.size() &&
             directories_scanned < maximum_directories &&
             discovered.size() < maximum_games) {
        const PendingFolder candidate = pending[next_folder++];
        ++directories_scanned;
        bool is_game = false;
        try {
          const IStorageItem eboot_item =
              co_await candidate.folder.TryGetItemAsync(L"eboot.bin");
          const IStorageItem sce_system_item =
              co_await candidate.folder.TryGetItemAsync(L"sce_sys");
          const StorageFolder sce_system =
              sce_system_item ? sce_system_item.try_as<StorageFolder>()
                              : nullptr;
          if (eboot_item && eboot_item.IsOfType(StorageItemTypes::File) &&
              sce_system) {
            const IStorageItem param_item =
                co_await sce_system.TryGetItemAsync(L"param.sfo");
            const StorageFile param_file =
                param_item ? param_item.try_as<StorageFile>() : nullptr;
            if (param_file) {
              const IBuffer buffer =
                  co_await FileIO::ReadBufferAsync(param_file);
              constexpr std::uint32_t maximum_sfo_size = 4U * 1024U * 1024U;
              if (buffer.Length() > 0U && buffer.Length() <= maximum_sfo_size) {
                std::vector<std::uint8_t> bytes(buffer.Length());
                const DataReader reader = DataReader::FromBuffer(buffer);
                reader.ReadBytes(bytes);
                const ::Core::Uwp::GameMetadata metadata =
                    ::Core::Uwp::ParseParamSfo(bytes);
                if (metadata.valid) {
                  DiscoveredGame game{};
                  game.display.title = CreateFolderLabel(metadata.title);
                  game.display.title_id = CreateFolderLabel(metadata.title_id);
                  game.display.app_version =
                      CreateFolderLabel(metadata.app_version);
                  game.folder_name = to_string(candidate.folder.Name());
                  const auto link_results_before = guest_link_results.size();
                  const auto relocation_results_before =
                      guest_relocation_results.size();
                  const auto import_results_before =
                      guest_import_results.size();

                  // First inspect a bounded 16 KiB prefix. The optional full
                  // snapshot below is data-only; never call the entry point.
                  // Inspection failure must not hide a game.
                  try {
                    const StorageFile executable = eboot_item.as<StorageFile>();
                    game.executable = executable;
                    const IRandomAccessStream stream =
                        co_await executable.OpenReadAsync();
                    const auto size = stream.Size();
                    const auto requested =
                        static_cast<std::uint32_t>(std::min<std::uint64_t>(
                            size, ::Core::Uwp::GuestPrefixLimit));
                    const DataReader header_reader(stream.GetInputStreamAt(0));
                    const auto loaded =
                        co_await header_reader.LoadAsync(requested);
                    std::vector<std::uint8_t> prefix(loaded);
                    header_reader.ReadBytes(prefix);
                    header_reader.DetachStream();
                    const auto inspection =
                        ::Core::Uwp::InspectGuestPrefix(prefix, size);
                    const auto plan = ::Core::Uwp::PlanGuestLoads(prefix, size);
                    guest_segment_results.push_back(
                        {game.display.title_id, plan.valid,
                         static_cast<std::uint32_t>(
                             plan.valid ? ERROR_SUCCESS : ERROR_BAD_EXE_FORMAT),
                         plan.Details() +
                             ";payload_loaded=0;self_adapter_ready=0"});
                    game.display.guest_header_status =
                        inspection.header_valid
                            ? (inspection.self_container
                                   ? "SELF HEADER OK  NOT BOOTED"
                                   : "ELF HEADER OK  NOT BOOTED")
                            : "EBOOT HEADER UNSUPPORTED";
                    guest_preflight_results.push_back(
                        {game.display.title_id, inspection.header_valid,
                         static_cast<std::uint32_t>(inspection.header_valid
                                                        ? ERROR_SUCCESS
                                                        : ERROR_BAD_EXE_FORMAT),
                         inspection.Details() +
                             ";file_bytes=" + std::to_string(size) +
                             ";prefix_bytes=" + std::to_string(loaded)});
                    // Read-only data snapshot, never executable memory. SELF
                    // logical offsets are mapped only by the bounded adapter.
                    try {
                      if (!plan.valid ||
                          size > ::Core::Uwp::GuestPayloadFileLimit ||
                          payload_files_attempted >= maximum_payload_files ||
                          size > maximum_payload_scan_bytes -
                                     payload_bytes_requested) {
                        guest_payload_results.push_back(
                            {game.display.title_id, false, ERROR_NOT_SUPPORTED,
                             "stage=payload_data_staging;guest_executed=0;game_"
                             "frame=0;"
                             "payload_loaded=0;error=plan_or_scan_budget_"
                             "unsupported"});
                      } else {
                        ++payload_files_attempted;
                        payload_bytes_requested += size;
                        std::vector<std::uint8_t> snapshot(
                            static_cast<std::size_t>(size));
                        std::uint32_t payload_loaded = 0;
                        {
                          const DataReader payload_reader(
                              stream.GetInputStreamAt(0));
                          payload_loaded = co_await payload_reader.LoadAsync(
                              static_cast<std::uint32_t>(size));
                          if (payload_loaded == size)
                            payload_reader.ReadBytes(snapshot);
                          payload_reader.DetachStream();
                        }
                        if (payload_loaded != size || stream.Size() != size ||
                            !std::equal(prefix.begin(), prefix.end(),
                                        snapshot.begin())) {
                          guest_payload_results.push_back(
                              {game.display.title_id, false, ERROR_READ_FAULT,
                               "stage=payload_data_staging;guest_executed=0;"
                               "game_frame=0;"
                               "payload_loaded=0;error=short_read_or_snapshot_"
                               "changed"});
                        } else {
                          // Games have no runtime export registry yet. Numeric
                          // fixture exports must never be registered here.
                          const auto data_link =
                              ::Core::Uwp::StageGuestDataLink(snapshot);
                          const auto &payload = data_link.payload;
                          guest_payload_results.push_back(
                              {game.display.title_id, payload.Ready(),
                               static_cast<std::uint32_t>(
                                   payload.Ready() ? ERROR_SUCCESS
                                                   : ERROR_BAD_EXE_FORMAT),
                               payload.Details() +
                                   ";payload_phase=pre_relocation" +
                                   ";file_bytes=" + std::to_string(size) +
                                   ";source=uwp_storage_snapshot;image_"
                                   "retained=0"});
                          if (payload.Ready())
                            game.display.guest_header_status =
                                inspection.self_container
                                    ? "SELF DATA OK  NOT BOOTED"
                                    : "ELF DATA OK  NOT BOOTED";
                          if (payload.Ready()) {
                            try {
                              const auto &manifest = data_link.manifest;
                              guest_link_results.push_back(
                                  {game.display.title_id, manifest.valid,
                                   static_cast<std::uint32_t>(
                                       manifest.valid ? ERROR_SUCCESS
                                                      : ERROR_BAD_EXE_FORMAT),
                                   manifest.Details() +
                                       ";source=uwp_storage_snapshot"});
                            } catch (...) {
                              guest_link_results.push_back(
                                  {game.display.title_id, false,
                                   ERROR_GEN_FAILURE,
                                   "stage=link_manifest;guest_executed=0;"
                                   "ready_for_boot=0;error=manifest_inspection_"
                                   "failed"});
                            }
                          }
                          guest_relocation_results.push_back(
                              {game.display.title_id, data_link.valid,
                               static_cast<std::uint32_t>(
                                   data_link.valid ? ERROR_SUCCESS
                                                   : ERROR_BAD_EXE_FORMAT),
                               data_link.Details() +
                                   ";source=uwp_storage_snapshot;image_"
                                   "retained=0"});
                          guest_import_results.push_back(
                              {game.display.title_id, data_link.imports.valid,
                               static_cast<std::uint32_t>(
                                   data_link.imports.valid
                                       ? ERROR_SUCCESS
                                       : ERROR_BAD_EXE_FORMAT),
                               data_link.imports.Details() +
                                   ";source=uwp_storage_snapshot"});
                          // The staged image is deliberately discarded here;
                          // library scanning does not create an executable
                          // instance.
                        }
                      }
                    } catch (const hresult_error &error) {
                      guest_payload_results.push_back(
                          {game.display.title_id, false,
                           static_cast<std::uint32_t>(error.code().value),
                           "stage=payload_data_staging;guest_executed=0;"
                           "payload_loaded=0;error=storage_read_failed"});
                    } catch (...) {
                      guest_payload_results.push_back(
                          {game.display.title_id, false, ERROR_GEN_FAILURE,
                           "stage=payload_data_staging;guest_executed=0;"
                           "payload_loaded=0;error=allocation_or_staging_"
                           "failed"});
                    }
                    stream.Close();
                  } catch (const hresult_error &error) {
                    guest_payload_results.push_back(
                        {game.display.title_id, false,
                         static_cast<std::uint32_t>(error.code().value),
                         "stage=payload_data_staging;guest_executed=0;payload_"
                         "loaded=0;error=header_storage_read_failed"});
                    guest_segment_results.push_back(
                        {game.display.title_id, false,
                         static_cast<std::uint32_t>(error.code().value),
                         "stage=segment_plan;guest_executed=0;game_frame=0;"
                         "payload_loaded=0;error=storage_read_failed"});
                    game.display.guest_header_status = "EBOOT READ FAILED";
                    guest_preflight_results.push_back(
                        {game.display.title_id, false,
                         static_cast<std::uint32_t>(error.code().value),
                         "stage=header_inspection;guest_executed=0;error="
                         "storage_read_failed"});
                  }

                  if (guest_link_results.size() == link_results_before) {
                    guest_link_results.push_back({game.display.title_id, false,
                                                  ERROR_NOT_SUPPORTED,
                                                  "stage=link_manifest;guest_"
                                                  "executed=0;ready_for_boot=0;"
                                                  "error=payload_not_ready"});
                  }
                  if (guest_relocation_results.size() ==
                      relocation_results_before) {
                    guest_relocation_results.push_back(
                        {game.display.title_id, false, ERROR_NOT_SUPPORTED,
                         "stage=data_relocation;guest_executed=0;ready_for_"
                         "boot=0;"
                         "relocations_applied=0;error=snapshot_or_data_link_"
                         "unavailable"});
                  }
                  if (guest_import_results.size() == import_results_before) {
                    guest_import_results.push_back(
                        {game.display.title_id, false, ERROR_NOT_SUPPORTED,
                         "stage=import_resolution;guest_executed=0;ready_for_"
                         "boot=0;"
                         "namespace_valid=0;error=snapshot_or_import_plan_"
                         "unavailable"});
                  }
                  try {
                    const IStorageItem icon_item =
                        co_await sce_system.TryGetItemAsync(L"icon0.png");
                    const StorageFile icon_file =
                        icon_item ? icon_item.try_as<StorageFile>() : nullptr;
                    if (icon_file) {
                      game.display.icon_state = GameIconState::Invalid;
                      try {
                        constexpr std::uint64_t maximum_encoded_icon_size =
                            8ULL * 1024ULL * 1024ULL;
                        constexpr std::uint32_t maximum_source_dimension =
                            4096U;
                        constexpr std::uint32_t maximum_decoded_dimension =
                            256U;
                        const auto properties =
                            co_await icon_file.GetBasicPropertiesAsync();
                        if (properties.Size() == 0U ||
                            properties.Size() > maximum_encoded_icon_size) {
                          game.display.icon_error = ERROR_FILE_TOO_LARGE;
                        } else {
                          const auto stream =
                              co_await icon_file.OpenReadAsync();
                          const BitmapDecoder decoder =
                              co_await BitmapDecoder::CreateAsync(stream);
                          const std::uint32_t source_width =
                              decoder.PixelWidth();
                          const std::uint32_t source_height =
                              decoder.PixelHeight();
                          if (source_width == 0U || source_height == 0U ||
                              source_width > maximum_source_dimension ||
                              source_height > maximum_source_dimension) {
                            game.display.icon_error = ERROR_INVALID_DATA;
                          } else {
                            const std::uint32_t largest_dimension =
                                std::max(source_width, source_height);
                            const std::uint32_t target_width =
                                largest_dimension <= maximum_decoded_dimension
                                    ? source_width
                                    : std::max(1U,
                                               source_width *
                                                   maximum_decoded_dimension /
                                                   largest_dimension);
                            const std::uint32_t target_height =
                                largest_dimension <= maximum_decoded_dimension
                                    ? source_height
                                    : std::max(1U,
                                               source_height *
                                                   maximum_decoded_dimension /
                                                   largest_dimension);
                            BitmapTransform transform;
                            transform.ScaledWidth(target_width);
                            transform.ScaledHeight(target_height);
                            const PixelDataProvider pixels =
                                co_await decoder.GetPixelDataAsync(
                                    BitmapPixelFormat::Bgra8,
                                    BitmapAlphaMode::Premultiplied, transform,
                                    ExifOrientationMode::IgnoreExifOrientation,
                                    ColorManagementMode::ColorManageToSRgb);
                            const com_array<std::uint8_t> detached =
                                pixels.DetachPixelData();
                            std::vector<std::uint8_t> decoded(detached.begin(),
                                                              detached.end());
                            const std::size_t expected_size =
                                static_cast<std::size_t>(target_width) *
                                target_height * 4U;
                            if (decoded.size() != expected_size) {
                              game.display.icon_error = ERROR_INVALID_DATA;
                            } else {
                              game.display.icon_width = target_width;
                              game.display.icon_height = target_height;
                              game.display.icon_hash = HashIcon(
                                  decoded, target_width, target_height);
                              game.display.icon_bgra8 = std::move(decoded);
                              game.display.icon_state = GameIconState::Ready;
                              game.display.icon_error = ERROR_SUCCESS;
                            }
                          }
                        }
                      } catch (const hresult_error &error) {
                        game.display.icon_error =
                            static_cast<std::uint32_t>(error.code().value);
                      } catch (...) {
                        game.display.icon_error = ERROR_GEN_FAILURE;
                      }
                    }
                  } catch (const hresult_error &error) {
                    game.display.icon_state = GameIconState::Invalid;
                    game.display.icon_error =
                        static_cast<std::uint32_t>(error.code().value);
                  } catch (...) {
                    game.display.icon_state = GameIconState::Invalid;
                    game.display.icon_error = ERROR_GEN_FAILURE;
                  }
                  discovered.push_back(std::move(game));
                  is_game = true;
                } else {
                  ++invalid_metadata;
                }
              } else {
                ++invalid_metadata;
              }
            }
          }

          if (!is_game && candidate.depth < maximum_depth) {
            const auto subfolders =
                co_await library_folder_access_.GetSubfoldersAsync(
                    candidate.folder);
            for (const StorageFolder &subfolder : subfolders) {
              if (pending.size() >= maximum_directories) {
                break;
              }
              pending.push_back({subfolder, candidate.depth + 1U});
            }
          }
        } catch (const hresult_error &) {
          // A protected or transient directory must not abort the whole scan.
        }
      }

      shell_state_.games.clear();
      game_executables_.clear();
      shell_state_.games.reserve(discovered.size());
      for (const DiscoveredGame &game : discovered) {
        shell_state_.games.push_back(game.display);
        game_executables_.push_back(game.executable);
      }
      shell_state_.library_scan_state = discovered.empty()
                                            ? LibraryScanState::Empty
                                            : LibraryScanState::Ready;
      library_operation_active_ = false;
      const std::uint32_t report_error = WriteLibraryScanReport(
          true, discovered.empty() ? "no_games_found" : "games_found",
          ERROR_SUCCESS, directories_scanned, invalid_metadata, discovered);
      const auto preflight_report_error =
          WriteReport(ApplicationData::Current().LocalFolder(),
                      L"phase5-preflight.jsonl", guest_preflight_results,
                      {"guest-preflight-scan", true, ERROR_SUCCESS,
                       "stage=header_inspection;guest_executed=0;inspected=" +
                           std::to_string(guest_preflight_results.size())});
      const auto segment_report_error = WriteReport(
          ApplicationData::Current().LocalFolder(), L"phase5-segments.jsonl",
          guest_segment_results,
          {"guest-segment-scan", true, ERROR_SUCCESS,
           "stage=segment_plan;guest_executed=0;payload_loaded=0;inspected=" +
               std::to_string(guest_segment_results.size())});
      const auto payload_ready = std::count_if(
          guest_payload_results.begin(), guest_payload_results.end(),
          [](const ProbeResult &result) { return result.passed; });
      const bool payload_all_ready = !guest_payload_results.empty() &&
                                     static_cast<std::size_t>(payload_ready) ==
                                         guest_payload_results.size();
      const auto payload_report_error = WriteReport(
          ApplicationData::Current().LocalFolder(), L"phase5-payload.jsonl",
          guest_payload_results,
          {"guest-payload-scan", payload_all_ready,
           static_cast<std::uint32_t>(payload_all_ready ? ERROR_SUCCESS
                                                        : ERROR_NOT_SUPPORTED),
           "stage=payload_data_staging;guest_executed=0;game_frame=0;ready=" +
               std::to_string(payload_ready) +
               ";inspected=" + std::to_string(guest_payload_results.size()) +
               ";files_attempted=" + std::to_string(payload_files_attempted) +
               ";bytes_requested=" + std::to_string(payload_bytes_requested)});
      AppendLifecycleEvent(
          session_id_, "guest-payload",
          ("guest_executed=0;ready=" + std::to_string(payload_ready) +
           ";report_error=" + std::to_string(payload_report_error))
              .c_str());
      AppendLifecycleEvent(session_id_, "guest-segments",
                           ("guest_executed=0;payload_loaded=0;report_error=" +
                            std::to_string(segment_report_error))
                               .c_str());
      const auto manifests_valid = std::count_if(
          guest_link_results.begin(), guest_link_results.end(),
          [](const ProbeResult &result) { return result.passed; });
      const bool links_all_valid = !guest_link_results.empty() &&
                                   static_cast<std::size_t>(manifests_valid) ==
                                       guest_link_results.size();
      const auto link_report_error = WriteReport(
          ApplicationData::Current().LocalFolder(), L"phase5-link.jsonl",
          guest_link_results,
          {"guest-link-scan", links_all_valid,
           static_cast<std::uint32_t>(links_all_valid ? ERROR_SUCCESS
                                                      : ERROR_NOT_SUPPORTED),
           "stage=link_manifest;guest_executed=0;game_frame=0;imports_resolved="
           "0;"
           "relocations_applied=0;ready_for_boot=0;valid=" +
               std::to_string(manifests_valid) +
               ";inspected=" + std::to_string(guest_link_results.size())});
      AppendLifecycleEvent(
          session_id_, "guest-link",
          ("guest_executed=0;valid=" + std::to_string(manifests_valid) +
           ";report_error=" + std::to_string(link_report_error))
              .c_str());
      const auto namespaces_valid = std::count_if(
          guest_import_results.begin(), guest_import_results.end(),
          [](const ProbeResult &result) { return result.passed; });
      const bool imports_all_valid =
          !guest_import_results.empty() &&
          static_cast<std::size_t>(namespaces_valid) ==
              guest_import_results.size();
      const auto import_report_error = WriteReport(
          ApplicationData::Current().LocalFolder(), L"phase5-imports.jsonl",
          guest_import_results,
          {"guest-import-scan", imports_all_valid,
           static_cast<std::uint32_t>(imports_all_valid ? ERROR_SUCCESS
                                                        : ERROR_NOT_SUPPORTED),
           "stage=import_resolution;guest_executed=0;game_frame=0;imports_"
           "resolved=0;"
           "runtime_exports_callable=0;ready_for_boot=0;registered_data_"
           "exports=0;"
           "numeric_imports_matched=0;valid=" +
               std::to_string(namespaces_valid) +
               ";inspected=" + std::to_string(guest_import_results.size())});
      AppendLifecycleEvent(
          session_id_, "guest-imports",
          ("guest_executed=0;valid=" + std::to_string(namespaces_valid) +
           ";report_error=" + std::to_string(import_report_error))
              .c_str());
      const auto relocation_valid = std::count_if(
          guest_relocation_results.begin(), guest_relocation_results.end(),
          [](const ProbeResult &result) { return result.passed; });
      const bool relocations_all_valid =
          !guest_relocation_results.empty() &&
          static_cast<std::size_t>(relocation_valid) ==
              guest_relocation_results.size();
      const auto relocation_report_error = WriteReport(
          ApplicationData::Current().LocalFolder(), L"phase5-relocations.jsonl",
          guest_relocation_results,
          {"guest-relocation-scan", relocations_all_valid,
           static_cast<std::uint32_t>(
               relocations_all_valid ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED),
           "stage=data_relocation;guest_executed=0;game_frame=0;ready_for_boot="
           "0;"
           "address_scope=synthetic_load_bias;valid=" +
               std::to_string(relocation_valid) + ";inspected=" +
               std::to_string(guest_relocation_results.size())});
      AppendLifecycleEvent(
          session_id_, "guest-relocations",
          ("guest_executed=0;valid=" + std::to_string(relocation_valid) +
           ";report_error=" + std::to_string(relocation_report_error))
              .c_str());
      AppendLifecycleEvent(
          session_id_, "guest-preflight",
          ("stage=header_inspection;guest_executed=0;inspected=" +
           std::to_string(guest_preflight_results.size()) +
           ";report_error=" + std::to_string(preflight_report_error))
              .c_str());
      const std::size_t icons_ready = std::count_if(
          discovered.begin(), discovered.end(), [](const DiscoveredGame &game) {
            return game.display.icon_state == GameIconState::Ready;
          });
      const std::size_t icons_invalid = std::count_if(
          discovered.begin(), discovered.end(), [](const DiscoveredGame &game) {
            return game.display.icon_state == GameIconState::Invalid;
          });
      const std::string_view icon_report_state =
          discovered.empty() ? "no_games"
                             : (icons_ready == discovered.size()
                                    ? "icons_ready"
                                    : (icons_ready == 0U ? "fallback_only"
                                                         : "partial_fallback"));
      if (report_error != ERROR_SUCCESS) {
        shell_state_.library_scan_state = LibraryScanState::Failed;
      }
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
      const bool selected_icon_presented =
          renderer_ && renderer_->SelectedGameIconReady();
      const std::uint32_t icon_report_error =
          WriteLibraryIconReport(true, icon_report_state, ERROR_SUCCESS,
                                 discovered, selected_icon_presented);
      const std::string lifecycle_details =
          "scan complete;games=" + std::to_string(discovered.size()) +
          ";directories=" + std::to_string(directories_scanned) +
          ";icons_ready=" + std::to_string(icons_ready) +
          ";icons_invalid=" + std::to_string(icons_invalid) +
          ";selected_icon_presented=" +
          std::to_string(selected_icon_presented ? 1U : 0U) +
          ";icon_report_error=" + std::to_string(icon_report_error);
      AppendLifecycleEvent(session_id_, "library-scan",
                           lifecycle_details.c_str());
    } catch (const hresult_error &error) {
      library_operation_active_ = false;
      shell_state_.library_scan_state = LibraryScanState::Failed;
      WriteLibraryScanReport(false, "scan_failed",
                             static_cast<std::uint32_t>(error.code().value),
                             directories_scanned, invalid_metadata, discovered);
      WriteLibraryIconReport(false, "scan_failed",
                             static_cast<std::uint32_t>(error.code().value),
                             discovered, false);
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
    } catch (...) {
      library_operation_active_ = false;
      shell_state_.library_scan_state = LibraryScanState::Failed;
      WriteLibraryScanReport(false, "scan_failed", ERROR_GEN_FAILURE,
                             directories_scanned, invalid_metadata, discovered);
      WriteLibraryIconReport(false, "scan_failed", ERROR_GEN_FAILURE,
                             discovered, false);
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
    }
  }

  void SelectCurrentLibraryFolder() {
    if (!current_folder_ || library_operation_active_) {
      return;
    }
    const std::string folder_name = to_string(current_folder_.Name());
    shell_state_.library_selection_confirmed = true;
    PersistLibrarySelection();
    std::ostringstream details;
    details << "source=KnownFolders.RemovableDevices;device_index=0;depth="
            << folder_stack_.size()
            << ";breadcrumb=" << shell_state_.library_breadcrumb;
    PresentLibraryState(true, "folder_selected", ERROR_SUCCESS, folder_name,
                        details.str());
    AppendLifecycleEvent(session_id_, "library-folder-selected",
                         "USB library folder selected;input_consumed=1");
    ScanSelectedLibrary();
  }

  void OnKeyDown(const CoreWindow &, const KeyEventArgs &args) noexcept {
    try {
      const VirtualKey key = args.VirtualKey();
      bool changed = false;
      if (shell_state_.page == XboxShellPage::Home) {
        if (key == VirtualKey::GamepadDPadLeft || key == VirtualKey::Left) {
          shell_state_.selected_item = (shell_state_.selected_item + 2U) % 3U;
          changed = true;
        } else if (key == VirtualKey::GamepadDPadRight ||
                   key == VirtualKey::Right) {
          shell_state_.selected_item = (shell_state_.selected_item + 1U) % 3U;
          changed = true;
        } else if (key == VirtualKey::GamepadA || key == VirtualKey::Enter) {
          constexpr XboxShellPage pages[]{XboxShellPage::Games,
                                          XboxShellPage::Settings,
                                          XboxShellPage::Diagnostics};
          shell_state_.page = pages[shell_state_.selected_item];
          changed = true;
        }
      } else if (shell_state_.page == XboxShellPage::Games) {
        if (library_operation_active_) {
          if (startup_operation_active_ &&
              (key == VirtualKey::GamepadB || key == VirtualKey::Escape)) {
            args.Handled(true);
            ++startup_generation_;
            library_operation_active_ = startup_operation_active_ = false;
            shell_state_.library_scan_state = LibraryScanState::Inactive;
            shell_state_.games.clear();
            ReleaseStartup("back_cancelled");
            if (renderer_)
              renderer_->Render(shell_state_);
            return;
          }
          if (key == VirtualKey::GamepadA || key == VirtualKey::GamepadB ||
              key == VirtualKey::Enter || key == VirtualKey::Escape) {
            args.Handled(true);
          }
          return;
        }

        if (shell_state_.library_folder_state == LibraryFolderState::Ready) {
          if (shell_state_.library_scan_state != LibraryScanState::Inactive) {
            if ((key == VirtualKey::GamepadDPadUp || key == VirtualKey::Up) &&
                !shell_state_.games.empty()) {
              shell_state_.selected_game =
                  (shell_state_.selected_game +
                   static_cast<std::uint32_t>(shell_state_.games.size()) - 1U) %
                  static_cast<std::uint32_t>(shell_state_.games.size());
              changed = true;
            } else if ((key == VirtualKey::GamepadDPadDown ||
                        key == VirtualKey::Down) &&
                       !shell_state_.games.empty()) {
              shell_state_.selected_game =
                  (shell_state_.selected_game + 1U) %
                  static_cast<std::uint32_t>(shell_state_.games.size());
              changed = true;
            } else if (key == VirtualKey::GamepadA ||
                       key == VirtualKey::Enter) {
              args.Handled(true);
              PrepareSelectedStartup();
              return;
            } else if (key == VirtualKey::GamepadX) {
              args.Handled(true);
              ReleaseStartup("rescan");
              ScanSelectedLibrary();
              return;
            } else if (key == VirtualKey::GamepadB ||
                       key == VirtualKey::Escape) {
              shell_state_.library_scan_state = LibraryScanState::Inactive;
              shell_state_.games.clear();
              shell_state_.selected_game = 0U;
              ReleaseStartup("back");
              changed = true;
            }
          } else if ((key == VirtualKey::GamepadDPadUp ||
                      key == VirtualKey::Up) &&
                     !child_folders_.empty()) {
            shell_state_.selected_library_entry =
                (shell_state_.selected_library_entry +
                 static_cast<std::uint32_t>(child_folders_.size()) - 1U) %
                static_cast<std::uint32_t>(child_folders_.size());
            shell_state_.library_selection_confirmed = false;
            changed = true;
          } else if ((key == VirtualKey::GamepadDPadDown ||
                      key == VirtualKey::Down) &&
                     !child_folders_.empty()) {
            shell_state_.selected_library_entry =
                (shell_state_.selected_library_entry + 1U) %
                static_cast<std::uint32_t>(child_folders_.size());
            shell_state_.library_selection_confirmed = false;
            changed = true;
          } else if (key == VirtualKey::GamepadA || key == VirtualKey::Enter) {
            args.Handled(true);
            OpenSelectedFolder();
            return;
          } else if (key == VirtualKey::GamepadX) {
            args.Handled(true);
            SelectCurrentLibraryFolder();
            return;
          } else if (key == VirtualKey::GamepadB || key == VirtualKey::Escape) {
            args.Handled(true);
            if (folder_stack_.empty()) {
              shell_state_.page = XboxShellPage::Home;
              changed = true;
            } else {
              OpenParentFolder();
              return;
            }
          }
        } else if (key == VirtualKey::GamepadA || key == VirtualKey::Enter) {
          args.Handled(true);
          DetectRemovableStorage();
          return;
        } else if (key == VirtualKey::GamepadB || key == VirtualKey::Escape) {
          shell_state_.page = XboxShellPage::Home;
          changed = true;
        }
      } else if (key == VirtualKey::GamepadB || key == VirtualKey::Escape) {
        shell_state_.page = XboxShellPage::Home;
        changed = true;
      }

      if (changed) {
        // Prevent Xbox shell navigation from also handling the same gamepad
        // button. In particular, an unhandled GamepadB returns to Dev Home.
        args.Handled(true);
        if (renderer_) {
          renderer_->Render(shell_state_);
          AppendLifecycleEvent(
              session_id_, "navigation",
              "Xbox Shell navigation event presented;input_consumed=1");
        }
      }
    } catch (...) {
      // Navigation failure must not terminate the UWP event loop.
    }
  }

  void UpsertResult(ProbeResult result) {
    const auto existing = std::find_if(results_.begin(), results_.end(),
                                       [&result](const ProbeResult &candidate) {
                                         return candidate.name == result.name;
                                       });
    if (existing == results_.end()) {
      results_.push_back(std::move(result));
    } else {
      *existing = std::move(result);
    }
  }

  void PersistReport() {
    if (renderer_ && renderer_->SubmissionReady()) {
      try {
        UpsertResult({"d3d12-submission", true, ERROR_SUCCESS,
                      renderer_->SubmissionDetails()});
      } catch (const hresult_error &error) {
        UpsertResult({"d3d12-submission", false,
                      static_cast<std::uint32_t>(error.code().value),
                      to_string(error.message())});
      }
      const auto stats = renderer_->ResourceStats();
      const bool resource_accounting_passed =
          stats.failed_allocations == 0U &&
          stats.live_bytes <= stats.budget_bytes &&
          stats.peak_bytes <= stats.budget_bytes &&
          stats.default_bytes + stats.upload_bytes + stats.readback_bytes ==
              stats.live_bytes;
      UpsertResult({"d3d12-resources", resource_accounting_passed,
                    static_cast<std::uint32_t>(stats.last_error),
                    renderer_->ResourceDetails()});
      UpsertResult({"d3d12-pipelines", renderer_->PipelineProbePassed(),
                    static_cast<std::uint32_t>(renderer_->PipelineProbePassed()
                                                   ? ERROR_SUCCESS
                                                   : ERROR_INVALID_DATA),
                    renderer_->PipelineDetails()});
      UpsertResult({"d3d12-shaders", renderer_->ShaderProbePassed(),
                    static_cast<std::uint32_t>(renderer_->ShaderProbePassed()
                                                   ? ERROR_SUCCESS
                                                   : ERROR_INVALID_DATA),
                    renderer_->ShaderDetails()});
      UpsertResult({"d3d12-shader-push-data", renderer_->PushDataProbePassed(),
                    static_cast<std::uint32_t>(renderer_->PushDataProbePassed()
                                                   ? ERROR_SUCCESS
                                                   : ERROR_INVALID_DATA),
                    renderer_->PushDataDetails()});
      UpsertResult(
          {"d3d12-shader-upstream", renderer_->UpstreamShaderProbePassed(),
           static_cast<std::uint32_t>(renderer_->UpstreamShaderProbePassed()
                                          ? ERROR_SUCCESS
                                          : ERROR_INVALID_DATA),
           renderer_->UpstreamShaderDetails()});
      UpsertResult(
          {"d3d12-shader-graphics", renderer_->GraphicsShaderProbePassed(),
           static_cast<std::uint32_t>(renderer_->GraphicsShaderProbePassed()
                                          ? ERROR_SUCCESS
                                          : ERROR_INVALID_DATA),
           renderer_->GraphicsShaderDetails()});
      UpsertResult({"d3d12-transfers", renderer_->TransferProbePassed(),
                    static_cast<std::uint32_t>(renderer_->TransferProbePassed()
                                                   ? ERROR_SUCCESS
                                                   : ERROR_INVALID_DATA),
                    renderer_->TransferDetails()});
      UpsertResult({"d3d12-videocore", renderer_->VideoCoreProbePassed(),
                    static_cast<std::uint32_t>(renderer_->VideoCoreProbePassed()
                                                   ? ERROR_SUCCESS
                                                   : ERROR_INVALID_DATA),
                    renderer_->VideoCoreDetails()});
    }
    std::erase_if(results_, [](const ProbeResult &result) {
      return result.name == "report-storage";
    });
    results_.push_back(SaveReport(results_));
  }

  void SaveStartupReport(ProbeResult result) {
    result.details += ";session=" + session_id_;
    if (startup_reports_.size() >= 16)
      startup_reports_.erase(startup_reports_.begin());
    startup_reports_.push_back(std::move(result));
    const auto error =
        WriteReport(ApplicationData::Current().LocalFolder(),
                    L"phase5-startup.jsonl", startup_reports_,
                    {"startup-report-storage", true, ERROR_SUCCESS,
                     "guest_entry_called=0;game_frame=0"});
    const auto details = "report_error=" + std::to_string(error) +
                         ";guest_entry_called=0;game_frame=0";
    AppendLifecycleEvent(session_id_, "guest-startup", details.c_str());
  }
  void ReleaseStartup(const char *reason) {
    if (!startup_session_)
      return;
    const bool released = startup_session_->Release();
    SaveStartupReport(
        {startup_title_ + "-release", released,
         released ? DWORD{ERROR_SUCCESS} : DWORD{ERROR_INVALID_DATA},
         startup_session_->Details() + ";release_reason=" + reason});
    if (released)
      startup_session_.reset();
  }
  fire_and_forget PrepareSelectedStartup() {
    [[maybe_unused]] const auto lifetime = get_strong();
    const auto selected = shell_state_.selected_game;
    if (library_operation_active_ || selected >= game_executables_.size() ||
        selected >= shell_state_.games.size() || !game_executables_[selected])
      co_return;
    const StorageFile file = game_executables_[selected];
    const auto title = shell_state_.games[selected].title_id;
    const auto generation = ++startup_generation_;
    library_operation_active_ = startup_operation_active_ = true;
    shell_state_.games[selected].guest_header_status =
        "PREPARING STARTUP  NOT BOOTED";
    try {
      ReleaseStartup("next_attempt");
      if (startup_session_)
        throw hresult_error(E_FAIL, L"startup cleanup failed");
      if (renderer_)
        renderer_->Render(shell_state_);
      const auto stream = co_await file.OpenReadAsync();
      const auto size = stream.Size();
      if (!size || size > ::Core::Uwp::GuestPayloadFileLimit)
        throw hresult_error(E_INVALIDARG, L"startup file budget exceeded");
      const auto requested = static_cast<std::uint32_t>(
          std::min<std::uint64_t>(size, ::Core::Uwp::GuestPrefixLimit));
      const DataReader prefix_reader(stream.GetInputStreamAt(0));
      const auto prefix_size = co_await prefix_reader.LoadAsync(requested);
      if (generation != startup_generation_)
        co_return;
      std::vector<std::uint8_t> prefix(prefix_size);
      prefix_reader.ReadBytes(prefix);
      prefix_reader.DetachStream();
      if (prefix_size != requested)
        throw hresult_error(E_FAIL, L"startup prefix short read");
      const DataReader reader(stream.GetInputStreamAt(0));
      const auto loaded =
          co_await reader.LoadAsync(static_cast<std::uint32_t>(size));
      if (generation != startup_generation_)
        co_return; // Suspension/back cancels before any native mapping is
                   // created.
      if (loaded != size || stream.Size() != size)
        throw hresult_error(E_FAIL, L"startup snapshot changed or short read");
      std::vector<std::uint8_t> snapshot(loaded);
      reader.ReadBytes(snapshot);
      reader.DetachStream();
      if (!std::equal(prefix.begin(), prefix.end(), snapshot.begin()))
        throw hresult_error(E_FAIL, L"startup snapshot prefix changed");
      startup_session_ = std::make_unique<GuestStartupSession>();
      startup_title_ = title;
      const bool prepared = startup_session_->Prepare(snapshot);
      SaveStartupReport(
          {title, prepared,
           prepared ? DWORD{ERROR_SUCCESS} : DWORD{ERROR_INVALID_DATA},
           startup_session_->Details() + ";source=uwp_storage_snapshot"});
      shell_state_.games[selected].guest_header_status =
          prepared ? "STARTUP BLOCKED  SEE LOG" : "STARTUP PREPARATION FAILED";
    } catch (const hresult_error &error) {
      if (generation != startup_generation_)
        co_return;
      shell_state_.games[selected].guest_header_status =
          "STARTUP READ OR PREP FAILED";
      SaveStartupReport(
          {title, false, static_cast<DWORD>(error.code().value),
           "stage=startup_preparation;blocker=storage_or_preparation_error;"
           "guest_entry_called=0;game_executed=0;game_frame=0"});
    } catch (...) {
      if (generation != startup_generation_)
        co_return;
      shell_state_.games[selected].guest_header_status =
          "STARTUP PREPARATION FAILED";
      AppendLifecycleEvent(session_id_, "guest-startup",
                           "unexpected preparation or report failure");
    }
    library_operation_active_ = startup_operation_active_ = false;
    try {
      if (renderer_)
        renderer_->Render(shell_state_);
    } catch (...) {
      AppendLifecycleEvent(session_id_, "guest-startup",
                           "presentation failed after preparation");
    }
  }

  void OnActivated(const CoreApplicationView &, const IActivatedEventArgs &) {
    CoreWindow::GetForCurrentThread().Activate();
  }

  void OnSuspending(const IInspectable &,
                    const SuspendingEventArgs &args) noexcept {
    const auto deferral = args.SuspendingOperation().GetDeferral();
    const std::uint32_t journal_error = AppendLifecycleEvent(
        session_id_, "suspend", "suspending event observed;report flushed");
    try {
      ++startup_generation_;
      if (startup_operation_active_)
        library_operation_active_ = startup_operation_active_ = false;
      ReleaseStartup("suspend");
      bool trim_supported = false;
      if (renderer_) {
        trim_supported = renderer_->TryTrim();
      }
      const std::string trim_details =
          trim_supported ? "dxgi_trim_supported=1;dxgi_trim_called=1"
                         : "dxgi_trim_supported=0;dxgi_trim_called=0";
      UpsertResult({"lifecycle-suspend", journal_error == ERROR_SUCCESS,
                    journal_error,
                    "suspending event observed;gpu_drained=1;" + trim_details +
                        ";session=" + session_id_});
    } catch (const hresult_error &error) {
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

  void OnResuming(const IInspectable &, const IInspectable &) noexcept {
    const std::uint32_t journal_error =
        AppendLifecycleEvent(session_id_, "resume", "resuming event observed");
    try {
      if (!renderer_) {
        UpsertResult({"lifecycle-resume", false, ERROR_INVALID_HANDLE,
                      "resuming event observed without an active renderer"});
      } else {
        renderer_->Render(shell_state_);
        UpsertResult({"lifecycle-resume", journal_error == ERROR_SUCCESS,
                      journal_error,
                      "resuming event observed;D3D12 Xbox Shell presented "
                      "again;session=" +
                          session_id_});
      }
    } catch (const hresult_error &error) {
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
  ::Core::Uwp::BridgeStatus bridge_status_{};
  XboxShellState shell_state_{};
  LibraryFolderAccess library_folder_access_{};
  StorageFolder current_folder_{nullptr};
  std::vector<StorageFolder> child_folders_;
  std::vector<StorageFolder> folder_stack_;
  bool library_operation_active_{};
  std::vector<StorageFile> game_executables_;
  std::unique_ptr<GuestStartupSession> startup_session_;
  std::string startup_title_;
  std::vector<ProbeResult> startup_reports_;
  std::uint64_t startup_generation_{};
  bool startup_operation_active_{};
  std::unique_ptr<D3D12StatusRenderer> renderer_;
};

class ViewProviderFactory
    : public implements<ViewProviderFactory, IFrameworkViewSource> {
public:
  IFrameworkView CreateView() { return make<ViewProvider>(); }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  CoreApplication::Run(make<ViewProviderFactory>());
  return 0;
}
