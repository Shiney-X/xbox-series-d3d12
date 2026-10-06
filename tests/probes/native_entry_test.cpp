// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "native_entry_runtime.h"

int main() {
    const auto result = ProbeNativeEntry();
    std::cout << result.details << '\n';
    return result.passed && NativeEntryGatePassed() ? 0 : 1;
}
