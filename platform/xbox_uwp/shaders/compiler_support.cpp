// SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12
// contributors SPDX-License-Identifier: GPL-2.0-or-later
#include "common/logging/compiler_log.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Common::CompilerLog {
void Write(std::string_view message) {
#ifdef _WIN32
  const std::string line = std::string(message) + '\n';
  OutputDebugStringA(line.c_str());
#else
  std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()),
               message.data());
#endif
}
} // namespace Common::CompilerLog

// Compiler invariant failures abort this probe through its exception boundary.
// They must never silently become a successful shader or fall back to authored
// SPIR-V.
void assert_fail_impl() {
  throw std::runtime_error("shadPS4 shader compiler assertion");
}
[[noreturn]] void unreachable_impl() {
  throw std::runtime_error("shadPS4 shader compiler unsupported operation");
}
