// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace Core::Uwp {
// Preserve nested probe evidence without overriding parent fields such as
// guest_executed/game_executed/error. Prefixes are fixed by the caller.
inline std::string ScopeProbeFields(std::string_view prefix, std::string_view fields) {
    std::string out;
    while (!fields.empty()) {
        const auto end = fields.find(';');
        const auto field = fields.substr(0, end);
        if (!field.empty()) {
            out += ';';
            out += prefix;
            out += field;
        }
        if (end == std::string_view::npos)
            break;
        fields.remove_prefix(end + 1);
    }
    return out;
}
} // namespace Core::Uwp
