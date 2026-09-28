// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore {

// Capabilities requested for a cached image, independent of native API flags.
enum class ImageUsage : u32 {
    None = 0,
    TransferSource = 1u << 0,
    TransferDestination = 1u << 1,
    Sampled = 1u << 2,
    ColorAttachment = 1u << 3,
    DepthStencilAttachment = 1u << 4,
    Storage = 1u << 5,
};

constexpr ImageUsage operator|(ImageUsage left, ImageUsage right) noexcept {
    return static_cast<ImageUsage>(static_cast<u32>(left) | static_cast<u32>(right));
}

constexpr bool HasUsage(ImageUsage flags, ImageUsage usage) noexcept {
    return (static_cast<u32>(flags) & static_cast<u32>(usage)) != 0;
}

// The cache currently requests storage for color/compressed images up front
// to avoid recreating resources for compute clears or uncompressed views.
constexpr ImageUsage CachedImageUsage(bool is_block_compressed, bool is_depth) noexcept {
    const auto common =
        ImageUsage::TransferSource | ImageUsage::TransferDestination | ImageUsage::Sampled;
    if (is_block_compressed) {
        return common | ImageUsage::Storage;
    }
    if (is_depth) {
        return common | ImageUsage::DepthStencilAttachment;
    }
    return common | ImageUsage::ColorAttachment | ImageUsage::Storage;
}

} // namespace VideoCore
