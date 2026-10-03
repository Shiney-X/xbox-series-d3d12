// SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>
#include <fmt/format.h>

// Standalone compiler diagnostics: no desktop logger registry, filesystem or settings.
namespace Common::CompilerLog {
void Write(std::string_view message);
}
#define LOG_TRACE(log_class, ...) (void(0))
#define LOG_DEBUG(log_class, ...) Common::CompilerLog::Write(fmt::format(__VA_ARGS__))
#define LOG_INFO(log_class, ...) Common::CompilerLog::Write(fmt::format(__VA_ARGS__))
#define LOG_WARNING(log_class, ...) Common::CompilerLog::Write(fmt::format(__VA_ARGS__))
#define LOG_ERROR(log_class, ...) Common::CompilerLog::Write(fmt::format(__VA_ARGS__))
#define LOG_CRITICAL(log_class, ...) Common::CompilerLog::Write(fmt::format(__VA_ARGS__))
