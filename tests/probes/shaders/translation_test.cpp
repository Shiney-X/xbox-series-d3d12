// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <stdexcept>
#include "spirv_hlsl_bridge.h"
#include "upstream_compute.h"

void Check(bool condition) {
    if (!condition) {
        throw std::runtime_error("translation contract failed");
    }
}
template <class Action>
void Reject(Action action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("unsupported SPIR-V accepted");
}
int main() {
    try {
        using Xbox::Shaders::GraphicsStage;
        for (bool vertex : {true, false}) {
            const auto stage = vertex ? GraphicsStage::Vertex : GraphicsStage::Fragment;
            const auto words = Xbox::Shaders::EmitUpstreamGraphics(vertex);
            const auto graphics = Xbox::Shaders::TranslateGraphics(words, stage);
            std::cout << graphics.hlsl << '\n';
            Check(graphics.push_constant_words == 30);
            Check(graphics.constant_space == (vertex ? 1u : 2u));
            Check(graphics.hlsl.find(vertex ? "register(b0, space1)" : "register(b0, space2)") !=
                  std::string::npos);
            Check(words == Xbox::Shaders::EmitUpstreamGraphics(vertex));
            Reject([&] {
                (void)Xbox::Shaders::TranslateGraphics(words, vertex ? GraphicsStage::Fragment
                                                                     : GraphicsStage::Vertex);
            });
            Reject([&] { (void)Xbox::Shaders::TranslateCompute(words); });
            for (std::size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                if ((words[offset] & 0xffff) == 71 && count == 4 && words[offset + 2] == 30) {
                    auto invalid = words;
                    invalid[offset + 3] = 7;
                    Reject([&] { (void)Xbox::Shaders::TranslateGraphics(invalid, stage); });
                }
                offset += count;
            }
        }
        const auto upstream = Xbox::Shaders::EmitUpstreamCompute();
        const auto upstream_translation = Xbox::Shaders::TranslateCompute(upstream);
        std::cout << "Upstream emitted HLSL:\n" << upstream_translation.hlsl << '\n';
        Check(upstream_translation.push_constant_words == 30);
        Check(upstream_translation.local_size == std::array<std::uint32_t, 3>{2, 2, 1});
        Check(upstream == Xbox::Shaders::EmitUpstreamCompute());
        Check(Xbox::Shaders::TranslateCompute(Xbox::Shaders::EmitUpstreamCompute(200)).hlsl !=
              upstream_translation.hlsl);
        const auto fixture = Xbox::Shaders::ComputeFixture();
        const auto result = Xbox::Shaders::TranslateCompute(fixture);
        Check(result.local_size == std::array<std::uint32_t, 3>{2, 2, 1});
        Check(result.hlsl.find("RWTexture2D<uint>") != std::string::npos);
        Check(result.hlsl.find("register(u0") != std::string::npos);
        Check(result.hlsl.find("SV_DispatchThreadID") != std::string::npos);
        Check(result.hlsl == Xbox::Shaders::TranslateCompute(fixture).hlsl);
        Check(result.push_constant_words == 0);
        const auto push_fixture = Xbox::Shaders::PushDataFixture();
        const auto push_translation = Xbox::Shaders::TranslateCompute(push_fixture);
        Check(push_translation.push_constant_words == 30);
        Check(push_translation.hlsl.find("register(b0, space0)") != std::string::npos);
        Check(push_translation.hlsl == Xbox::Shaders::TranslateCompute(push_fixture).hlsl);
        const auto packed = Xbox::Shaders::EncodePushData(Xbox::Shaders::ProbePushData());
        Check(packed[0] == 0x3f800000 && packed[4] == 1 && packed[19] == 16);
        Check(packed[20] == 0x04030201 && packed[29] == 0x28272625);
        for (std::size_t offset = 5; offset < push_fixture.size();) {
            const auto count = push_fixture[offset] >> 16;
            const auto opcode = push_fixture[offset] & 0xffff;
            if (opcode == 72 && count == 5 && push_fixture[offset + 3] == 35) {
                auto bad_layout = push_fixture;
                bad_layout[offset + 4] += 4;
                Reject([&] { (void)Xbox::Shaders::TranslateCompute(bad_layout); });
            }
            if (opcode == 30 && count == 13) {
                auto bad_type = push_fixture;
                bad_type[offset + 2] = 2; // uint instead of float xoffset.
                Reject([&] { (void)Xbox::Shaders::TranslateCompute(bad_type); });
            }
            offset += count;
        }
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
                Check(changed.hlsl != result.hlsl &&
                      changed.hlsl.find("200u") != std::string::npos);
            }
            offset += count;
        }
        std::cout << result.hlsl << "\nSPIR-V/HLSL contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
