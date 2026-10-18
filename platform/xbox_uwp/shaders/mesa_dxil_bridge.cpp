// SPDX-FileCopyrightText: 2026 KytyPS5 contributors
// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-only
#include "mesa_dxil_bridge.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <spirv_to_dxil.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Xbox::Shaders {
namespace {
using Microsoft::WRL::ComPtr;
struct Module {
  HMODULE value{};
  ~Module() {
    if (value)
      FreeLibrary(value);
  }
};
// Owned COM wrapper; the byte range stays alive through validation/result.
class ByteBlob final : public IDxcBlob {
public:
  explicit ByteBlob(std::vector<std::uint8_t> &bytes) : bytes_(bytes) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != __uuidof(IUnknown) && id != __uuidof(IDxcBlob))
      return E_NOINTERFACE;
    *out = static_cast<IDxcBlob *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto refs = --refs_;
    if (!refs)
      delete this;
    return refs;
  }
  LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return bytes_.data(); }
  SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return bytes_.size(); }

private:
  std::atomic<ULONG> refs_{1};
  std::vector<std::uint8_t> &bytes_;
};
void Check(HRESULT hr, const char *step) {
  if (FAILED(hr))
    throw std::runtime_error(std::string(step) + ":" +
                             std::to_string(static_cast<std::uint32_t>(hr)));
}
dxil_spirv_shader_stage Stage(MesaStage stage) {
  switch (stage) {
  case MesaStage::Compute:
    return DXIL_SPIRV_SHADER_COMPUTE;
  case MesaStage::Vertex:
    return DXIL_SPIRV_SHADER_VERTEX;
  case MesaStage::Fragment:
    return DXIL_SPIRV_SHADER_FRAGMENT;
  }
  throw std::invalid_argument("mesa_stage_unsupported");
}
struct Objects {
  std::array<dxil_spirv_object, 2> values{};
  bool owned{};
  ~Objects() {
    if (owned)
      for (auto &value : values)
        spirv_to_dxil_free(&value);
  }
};
} // namespace

MesaTranslation TranslateMesaDxil(ID3D12Device *device,
                                  std::span<const MesaStageInput> stages) {
  MesaTranslation out;
  // Reject before entering Mesa, which expects structurally valid inputs.
  if (!device || stages.empty() || stages.size() > 2 ||
      (stages.size() == 1 && stages[0].stage != MesaStage::Compute) ||
      (stages.size() == 2 && (stages[0].stage != MesaStage::Vertex ||
                              stages[1].stage != MesaStage::Fragment))) {
    out.error = "mesa_pipeline_stage_order_invalid";
    return out;
  }
  for (const auto &stage : stages) {
    if (stage.words.size() < 5 || stage.words.size() > 16 * 1024 * 1024 ||
        stage.words[0] != 0x07230203 || stage.words[4] != 0 ||
        stage.push_constant_space == 1001) {
      out.error = "mesa_spirv_header_or_binding_invalid";
      return out;
    }
  }
  try {
    // Same conservative negotiation as Kyty: the device AND validator bound
    // the shader model. Do not assume Series X capabilities on Series S.
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_8};
    HRESULT query{};
    while (FAILED(query = device->CheckFeatureSupport(
                      D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
           model.HighestShaderModel > D3D_SHADER_MODEL_6_0)
      model.HighestShaderModel =
          static_cast<D3D_SHADER_MODEL>(model.HighestShaderModel - 1);
    Check(query, "mesa_shader_model_query");
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options{};
    out.lower_16bit =
        FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4,
                                           &options, sizeof(options))) ||
        !options.Native16BitShaderOpsSupported;
    Module module;
#if defined(XBOX_MESA_UWP)
    module.value = LoadPackagedLibrary(L"dxil.dll", 0);
#else
    // CI desktop only: never search the current directory or PATH.
    module.value = LoadLibraryExW(L"dxil.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
    if (!module.value)
      throw std::runtime_error("mesa_validator_load:" +
                               std::to_string(GetLastError()));
    const auto create = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(module.value, "DxcCreateInstance"));
    if (!create)
      throw std::runtime_error("mesa_validator_factory_missing");
    ComPtr<IDxcValidator> validator;
    Check(create(CLSID_DxcValidator, IID_PPV_ARGS(&validator)),
          "mesa_validator_create");
    ComPtr<IDxcVersionInfo> version;
    Check(validator.As(&version), "mesa_validator_version_query");
    UINT32 major{}, minor{};
    Check(version->GetVersion(&major, &minor), "mesa_validator_version");
    if (major != 1)
      throw std::runtime_error("mesa_validator_major_unsupported");
    out.validator_minor = std::min<std::uint32_t>(minor, 8);
    const auto device_minor =
        static_cast<std::uint32_t>(model.HighestShaderModel) & 0xfu;
    out.shader_model_minor = std::min(device_minor, out.validator_minor);
    const auto shader_model = static_cast<dxil_shader_model>(
        SHADER_MODEL_6_0 + out.shader_model_minor);
    const auto validator_version = static_cast<dxil_validator_version>(
        DXIL_VALIDATOR_1_0 + out.validator_minor);
    std::array<dxil_spirv_runtime_conf, 2> confs{};
    std::array<dxil_spirv_stage, 2> inputs{};
    for (std::size_t i = 0; i < stages.size(); ++i) {
      auto &conf = confs[i];
      conf.runtime_data_cbv.register_space = 1001;
      conf.push_constant_cbv.register_space = stages[i].push_constant_space;
      conf.first_vertex_and_base_instance_mode =
          stages[i].zero_base_vertex ? DXIL_SPIRV_SYSVAL_TYPE_ZERO
                                     : DXIL_SPIRV_SYSVAL_TYPE_RUNTIME_DATA;
      conf.workgroup_id_mode = DXIL_SPIRV_SYSVAL_TYPE_NATIVE;
      conf.shader_model_max = shader_model;
      conf.lower_16bit_ops = out.lower_16bit;
      conf.shadow_sampler_space_offset = 500;
      inputs[i] = {stages[i].words.data(),
                   stages[i].words.size(),
                   nullptr,
                   0,
                   Stage(stages[i].stage),
                   "main",
                   &conf};
    }
    std::string messages;
    const dxil_spirv_logger logger{
        &messages, [](void *state, const char *text) {
          auto &log = *static_cast<std::string *>(state);
          if (text && log.size() < 16384)
            log.append(text, std::min<std::size_t>(std::strlen(text),
                                                   16384 - log.size()));
        }};
    const dxil_spirv_debug_options debug{};
    Objects objects;
    const bool translated =
        stages.size() == 1
            ? spirv_to_dxil(inputs[0].words, inputs[0].word_count, nullptr, 0,
                            inputs[0].stage, "main", validator_version, &debug,
                            &confs[0], &logger, &objects.values[0])
            : spirv_to_dxil_linked(inputs.data(), 2, validator_version, &debug,
                                   &logger, objects.values.data());
    out.diagnostics = messages;
    if (!translated)
      throw std::runtime_error("mesa_translation_failed:" + messages);
    objects.owned = true;
    for (std::size_t i = 0; i < stages.size(); ++i) {
      const auto &object = objects.values[i];
      if (!object.binary.buffer || !object.binary.size ||
          object.binary.size > 64 * 1024 * 1024)
        throw std::runtime_error("mesa_output_invalid");
      MesaShader shader;
      const auto *bytes =
          static_cast<const std::uint8_t *>(object.binary.buffer);
      shader.dxil.assign(bytes, bytes + object.binary.size);
      shader.requires_runtime_data = object.metadata.requires_runtime_data;
      shader.needs_draw_sysvals = object.metadata.needs_draw_sysvals;
      ComPtr<IDxcBlob> blob;
      blob.Attach(new ByteBlob(shader.dxil));
      ComPtr<IDxcOperationResult> validation;
      Check(validator->Validate(blob.Get(), DxcValidatorFlags_InPlaceEdit,
                                &validation),
            "mesa_validate");
      HRESULT status{};
      Check(validation->GetStatus(&status), "mesa_validation_status");
      if (FAILED(status)) {
        ComPtr<IDxcBlobEncoding> errors;
        Check(validation->GetErrorBuffer(&errors), "mesa_validation_errors");
        throw std::runtime_error(
            "mesa_validation_failed:" +
            (errors ? std::string(
                          static_cast<const char *>(errors->GetBufferPointer()),
                          errors->GetBufferSize())
                    : std::to_string(static_cast<std::uint32_t>(status))));
      }
      out.shaders.push_back(std::move(shader));
    }
    out.passed = true;
  } catch (const std::exception &error) {
    out.shaders.clear();
    out.error = error.what();
  }
  return out;
}
} // namespace Xbox::Shaders
