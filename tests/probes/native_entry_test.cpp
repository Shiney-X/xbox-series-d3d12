// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "native_entry_runtime.h"

int main() {
    const auto result = ProbeNativeEntry();
    std::cout << result.details << '\n';
    const bool details_preserved =
        result.details.find("raw_first_blocker=") != std::string::npos &&
        result.details.find("self_first_win32_error=") != std::string::npos &&
        result.details.find("negative_blocker=native_entry_prefix_unsupported") !=
            std::string::npos;
    return result.passed && NativeEntryGatePassed() && details_preserved ? 0 : 1;
}
