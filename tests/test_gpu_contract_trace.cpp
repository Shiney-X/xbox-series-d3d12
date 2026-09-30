// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "video_core/buffer_cache/buffer_sync_state.h"
#include "video_core/texture_cache/image_sync_state.h"

namespace {

using VideoCore::BufferAccess;
using VideoCore::ImageLayout;

BufferAccess ParseBufferAccess(std::string_view text) {
    if (text == "TransferWrite") {
        return BufferAccess::TransferWrite;
    }
    if (text == "ShaderRead") {
        return BufferAccess::ShaderRead;
    }
    ADD_FAILURE() << "Unknown buffer access: " << text;
    return BufferAccess::Initial;
}

VideoCore::ImageResourceState ParseImageState(std::string_view text) {
    if (text == "ShaderReadOnly") {
        return VideoCore::ImageStates::ShaderReadOnly;
    }
    if (text == "TransferDestination") {
        return VideoCore::ImageStates::TransferDestination;
    }
    ADD_FAILURE() << "Unknown image state: " << text;
    return {};
}

std::string_view Name(BufferAccess value) {
    switch (value) {
    case BufferAccess::Initial:
        return "Initial";
    case BufferAccess::TransferWrite:
        return "TransferWrite";
    case BufferAccess::ShaderRead:
        return "ShaderRead";
    default:
        ADD_FAILURE() << "Unexpected buffer access";
        return "?";
    }
}

std::string_view Name(ImageLayout value) {
    switch (value) {
    case ImageLayout::Undefined:
        return "Undefined";
    case ImageLayout::ShaderReadOnly:
        return "ShaderReadOnly";
    case ImageLayout::TransferDestination:
        return "TransferDestination";
    default:
        ADD_FAILURE() << "Unexpected image layout";
        return "?";
    }
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input{path};
    EXPECT_TRUE(input.is_open()) << path.string();
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::string ReadGoldenFile(const std::filesystem::path& path) {
    std::istringstream input{ReadFile(path)};
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.front() != '#') {
            output << line << '\n';
        }
    }
    return output.str();
}

TEST(GpuContractTrace, ReplaysNeutralTransitionsAgainstGoldenTrace) {
    const auto fixtures = std::filesystem::path{__FILE__}.parent_path() / "fixtures";
    std::istringstream input{ReadFile(fixtures / "phase2l_neutral_trace.txt")};
    VideoCore::BufferSyncState buffer;
    VideoCore::ImageSyncState image{{2, 2}};
    std::ostringstream output;
    std::string line;
    bool version_seen = false;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!version_seen) {
            ASSERT_EQ(line, "v1");
            version_seen = true;
            continue;
        }
        std::istringstream step{line};
        char kind{};
        std::string state;
        ASSERT_TRUE(static_cast<bool>(step >> kind >> state)) << line;
        if (kind == 'B') {
            u64 offset{};
            u64 size{};
            ASSERT_TRUE(static_cast<bool>(step >> offset >> size)) << line;
            const auto transition = buffer.Transition(ParseBufferAccess(state), offset, size);
            if (transition) {
                output << "B " << Name(transition->before) << ' ' << Name(transition->after) << ' '
                       << transition->offset << ' ' << transition->size << '\n';
            } else {
                output << "B -\n";
            }
        } else if (kind == 'I') {
            std::string range;
            ASSERT_TRUE(static_cast<bool>(step >> range)) << line;
            std::optional<VideoCore::SubresourceRange> requested;
            if (range != "all") {
                VideoCore::SubresourceRange value{};
                value.base.level = static_cast<u32>(std::stoul(range));
                ASSERT_TRUE(static_cast<bool>(step >> value.base.layer >> value.extent.levels >>
                                              value.extent.layers))
                    << line;
                requested = value;
            }
            const auto transitions = image.Transition(ParseImageState(state), requested);
            if (transitions.empty()) {
                output << "I -\n";
            }
            for (const auto& transition : transitions) {
                output << "I " << Name(transition.before.layout) << ' '
                       << Name(transition.after.layout) << ' ' << transition.range.base.level << ' '
                       << transition.range.base.layer << ' ' << transition.range.extent.levels
                       << ' ' << transition.range.extent.layers << ' '
                       << (transition.whole_resource ? "whole" : "sub") << '\n';
            }
        } else {
            FAIL() << "Unknown trace operation: " << line;
        }
    }
    ASSERT_TRUE(version_seen);
    EXPECT_EQ(output.str(), ReadGoldenFile(fixtures / "phase2l_neutral_trace.golden"));
}

} // namespace
