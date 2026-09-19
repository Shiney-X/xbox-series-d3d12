// SPDX-License-Identifier: GPL-2.0-or-later

#include "library_folder_access.h"

#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Pickers.h>

using namespace winrt;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::AccessCache;
using namespace winrt::Windows::Storage::Pickers;

bool LibraryFolderAccess::HasSavedFolder() const {
  return StorageApplicationPermissions::FutureAccessList().ContainsItem(Token);
}

Windows::Foundation::IAsyncOperation<StorageFolder>
LibraryFolderAccess::RestoreAsync() const {
  co_return co_await StorageApplicationPermissions::FutureAccessList()
      .GetFolderAsync(Token);
}

Windows::Foundation::IAsyncOperation<StorageFolder>
LibraryFolderAccess::PickAsync() const {
  FolderPicker picker;
  picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
  picker.FileTypeFilter().Append(L"*");

  StorageFolder folder = co_await picker.PickSingleFolderAsync();
  if (folder) {
    StorageApplicationPermissions::FutureAccessList().AddOrReplace(Token,
                                                                   folder);
  }
  co_return folder;
}
