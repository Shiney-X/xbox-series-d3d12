// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Xbox::Shaders {
struct ComputeTranslation {
  std::string hlsl;
  std::array<std::uint32_t, 3> local_size{};
  std::uint32_t descriptor_set{};
  std::uint32_t binding{};
};

// Initial contract: compute, literal workgroup size, one R32_UINT storage
// image at set=0/binding=0. Not a general guest shader translator.
[[nodiscard]] ComputeTranslation TranslateCompute(std::span<const std::uint32_t> words);
[[nodiscard]] std::vector<std::uint32_t> ComputeFixture();
} // namespace Xbox::Shaders
