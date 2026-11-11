/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainSpec.h"
#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

ShaderCodeFormat SpecFormatForBackend(Backend backend, uint16_t payloadFormat) {
  const bool binary = payloadFormat == BundleFormat::Payload::Binary;
  switch (backend) {
    case Backend::Vulkan:
      return ShaderCodeFormat::SPIRV;
    case Backend::Metal:
      return binary ? ShaderCodeFormat::METALLIB : ShaderCodeFormat::MSL;
    case Backend::WebGPU:
      return ShaderCodeFormat::WGSL;
    case Backend::D3D12:
      return binary ? ShaderCodeFormat::DXBC : ShaderCodeFormat::HLSL;
    default:
      return ShaderCodeFormat::GLSL;
  }
}

SpecValues SpecResolver::Resolve(SpecSource* source, Backend backend, const ProgramInfo& info,
                                 const std::string& shaderName, uint32_t vertIndex,
                                 uint32_t fragIndex) {
  if (source == nullptr || !source->specializationEnabled() || !source->isLoaded()) {
    return {};
  }
  // A format without an injector cannot consume the values, so capturing for it would only cost
  // the draw a scratch write. The gate is the registry, not a backend list.
  if (FindSpecInjector(SpecFormatForBackend(backend, source->payloadFormat())) == nullptr) {
    return {};
  }
  // The values come straight off the compiled program (the same fields the runtime binder
  // uploads); the bundle's spec table supplies the order and the array lengths.
  (void)vertIndex;
  auto fragHash = ComputeFragmentKeyHash(shaderName, fragIndex, source->profileTag());
  const auto* fields = source->findFragmentSpecFields(fragHash.hi, fragHash.lo);
  if (fields == nullptr) {
    return {};
  }
  const auto* program = AOTChainBuilder::ResolveChainProgram(&info);
  if (program == nullptr) {
    return {};
  }
  auto values = ResolveChainSpec(*program, *fields);
  source->recordSpecCapture(values);
  return values;
}

}  // namespace tgfx
