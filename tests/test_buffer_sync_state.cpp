// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/buffer_cache/buffer_sync_state.h"

namespace {

TEST(BufferSyncState, TracksAccessOrderWithoutNativeApiTypes) {
    VideoCore::BufferSyncState state;
    using VideoCore::BufferAccess;

    const auto upload = state.Transition(BufferAccess::TransferWrite, 0, 4096);
    ASSERT_TRUE(upload);
    EXPECT_EQ(upload->before, BufferAccess::Initial);
    EXPECT_EQ(upload->after, BufferAccess::TransferWrite);
    EXPECT_EQ(upload->offset, 0);
    EXPECT_EQ(upload->size, 4096);

    EXPECT_FALSE(state.Transition(BufferAccess::TransferWrite, 64, 4096));

    const auto shader = state.Transition(BufferAccess::ShaderRead, 128, 4096);
    ASSERT_TRUE(shader);
    EXPECT_EQ(shader->before, BufferAccess::TransferWrite);
    EXPECT_EQ(shader->after, BufferAccess::ShaderRead);
    EXPECT_EQ(shader->offset, 128);
    EXPECT_EQ(shader->size, 4096 - 128);

    const auto readback = state.Transition(BufferAccess::TransferRead, 0, 4096);
    ASSERT_TRUE(readback);
    EXPECT_EQ(readback->before, BufferAccess::ShaderRead);
    EXPECT_EQ(readback->after, BufferAccess::TransferRead);

    EXPECT_FALSE(state.Transition(BufferAccess::VertexRead, 4096, 4096));
    const auto vertex = state.Transition(BufferAccess::VertexRead, 0, 4096);
    ASSERT_TRUE(vertex);
    EXPECT_EQ(vertex->before, BufferAccess::TransferRead);
}

} // namespace
