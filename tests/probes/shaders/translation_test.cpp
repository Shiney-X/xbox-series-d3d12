// SPDX-License-Identifier: GPL-2.0-or-later
#include "spirv_hlsl_bridge.h"
#include <iostream>
#include <stdexcept>

void Check(bool condition) {
  if (!condition) {
    throw std::runtime_error("translation contract failed");
  }
}
template<class Action> void Reject(Action action) {
  try {
    action();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("unsupported SPIR-V accepted");
}
int main() {
  try {
    const auto fixture = Xbox::Shaders::ComputeFixture();
    const auto result = Xbox::Shaders::TranslateCompute(fixture);
    Check(result.local_size == std::array<std::uint32_t, 3>{2, 2, 1});
    Check(result.hlsl.find("RWTexture2D<uint>") != std::string::npos);
    Check(result.hlsl.find("register(u0") != std::string::npos);
    Check(result.hlsl.find("SV_DispatchThreadID") != std::string::npos);
    Check(result.hlsl == Xbox::Shaders::TranslateCompute(fixture).hlsl);
    Reject([] { (void)Xbox::Shaders::TranslateCompute({}); });
    for (std::size_t size = 1; size < fixture.size(); ++size) {
      Reject([&] { (void)Xbox::Shaders::TranslateCompute(std::span(fixture).first(size)); });
    }
    auto invalid = fixture;
    invalid[0] = 0;
    Reject([&] { (void)Xbox::Shaders::TranslateCompute(invalid); });
    invalid = fixture;
    invalid[5] &= 0xffff;
    Reject([&] { (void)Xbox::Shaders::TranslateCompute(invalid); });
    // OpDecorate(binding) / OpExecutionMode(LocalSize) mutations: no remap
    // or unsupported workgroup can silently match the fixed root signature.
    for (std::size_t offset = 5; offset < fixture.size();) {
      const auto count = fixture[offset] >> 16;
      const auto opcode = fixture[offset] & 0xffff;
      if (opcode == 71 && count == 4 && fixture[offset + 2] == 33) {
        invalid = fixture;
        invalid[offset + 3] = 1;
        Reject([&] { (void)Xbox::Shaders::TranslateCompute(invalid); });
      }
      if (opcode == 16 && count == 6) {
        invalid = fixture;
        invalid[offset + 3] = 1025;
        Reject([&] { (void)Xbox::Shaders::TranslateCompute(invalid); });
      }
      if (opcode == 71 && count == 4 && fixture[offset + 2] == 34) {
        invalid = fixture;
        invalid[offset + 3] = 1;
        Reject([&] { (void)Xbox::Shaders::TranslateCompute(invalid); });
      }
      if (opcode == 43 && count == 4 && fixture[offset + 3] == 100) {
        invalid = fixture;
        invalid[offset + 3] = 200;
        const auto changed = Xbox::Shaders::TranslateCompute(invalid);
        Check(changed.hlsl != result.hlsl && changed.hlsl.find("200u") != std::string::npos);
      }
      offset += count;
    }
    std::cout << result.hlsl << "\nSPIR-V/HLSL contract passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
