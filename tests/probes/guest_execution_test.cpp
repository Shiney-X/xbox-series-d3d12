// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "guest_execution_probe.h"

int main() {
    const auto result = ProbeGuestExecution();
    std::cout << result.details << '\n';
    return result.passed && result.error == 0 ? 0 : 1;
}
