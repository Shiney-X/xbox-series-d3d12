// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "library_folder_access.h"

#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace winrt::Windows::Storage;

Windows::Foundation::IAsyncOperation<StorageFolder>
LibraryFolderAccess::FindFirstRemovableDeviceAsync() const {
  const StorageFolder removable_devices = KnownFolders::RemovableDevices();
  const auto devices = co_await removable_devices.GetFoldersAsync();
  if (devices.Size() == 0U) {
    co_return StorageFolder{nullptr};
  }
  co_return devices.GetAt(0);
}
