// SPDX-License-Identifier: GPL-2.0-or-later

#include <winrt/base.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include "core/uwp/core_bridge.h"
#include "d3d12_status_renderer.h"
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
};

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
      shell_state_.games.reserve(discovered.size());
      for (const DiscoveredGame &game : discovered) {
        shell_state_.games.push_back(game.display);
      }
      shell_state_.library_scan_state = discovered.empty()
                                            ? LibraryScanState::Empty
                                            : LibraryScanState::Ready;
      library_operation_active_ = false;
      const std::uint32_t report_error = WriteLibraryScanReport(
          true, discovered.empty() ? "no_games_found" : "games_found",
          ERROR_SUCCESS, directories_scanned, invalid_metadata, discovered);
      if (report_error != ERROR_SUCCESS) {
        shell_state_.library_scan_state = LibraryScanState::Failed;
      }
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
      const std::string lifecycle_details =
          "scan complete;games=" + std::to_string(discovered.size()) +
          ";directories=" + std::to_string(directories_scanned);
      AppendLifecycleEvent(session_id_, "library-scan",
                           lifecycle_details.c_str());
    } catch (const hresult_error &error) {
      library_operation_active_ = false;
      shell_state_.library_scan_state = LibraryScanState::Failed;
      WriteLibraryScanReport(false, "scan_failed",
                             static_cast<std::uint32_t>(error.code().value),
                             directories_scanned, invalid_metadata, discovered);
      if (renderer_) {
        renderer_->Render(shell_state_);
      }
    } catch (...) {
      library_operation_active_ = false;
      shell_state_.library_scan_state = LibraryScanState::Failed;
      WriteLibraryScanReport(false, "scan_failed", ERROR_GEN_FAILURE,
                             directories_scanned, invalid_metadata, discovered);
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
            } else if (key == VirtualKey::GamepadX) {
              args.Handled(true);
              ScanSelectedLibrary();
              return;
            } else if (key == VirtualKey::GamepadB ||
                       key == VirtualKey::Escape) {
              shell_state_.library_scan_state = LibraryScanState::Inactive;
              shell_state_.games.clear();
              shell_state_.selected_game = 0U;
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
    std::erase_if(results_, [](const ProbeResult &result) {
      return result.name == "report-storage";
    });
    results_.push_back(SaveReport(results_));
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
      bool trim_supported = false;
      if (renderer_) {
        trim_supported = renderer_->TryTrim();
      }
      const std::string trim_details =
          trim_supported ? "dxgi_trim_supported=1;dxgi_trim_called=1"
                         : "dxgi_trim_supported=0;dxgi_trim_called=0";
      UpsertResult({"lifecycle-suspend", journal_error == ERROR_SUCCESS,
                    journal_error,
                    "suspending event observed;" + trim_details +
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
