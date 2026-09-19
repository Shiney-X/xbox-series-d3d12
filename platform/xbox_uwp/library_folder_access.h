// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

class LibraryFolderAccess final {
public:
  [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Storage::StorageFolder>
  FindFirstRemovableDeviceAsync() const;
};
