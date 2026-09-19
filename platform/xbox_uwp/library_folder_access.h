// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

class LibraryFolderAccess final {
public:
  [[nodiscard]] bool HasSavedFolder() const;
  [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Storage::StorageFolder>
  RestoreAsync() const;
  [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Storage::StorageFolder>
  PickAsync() const;

private:
  static constexpr wchar_t Token[] = L"shadps4-game-library";
};
