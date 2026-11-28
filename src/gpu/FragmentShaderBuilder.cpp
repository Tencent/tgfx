/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "FragmentShaderBuilder.h"
#include "ProgramBuilder.h"
#include "SLType.h"
#include "core/utils/Log.h"
#include "gpu/ir/ProgramIRBuilder.h"

namespace tgfx {
FragmentShaderBuilder::FragmentShaderBuilder(ProgramBuilder* program) : ShaderBuilder(program) {
}

void FragmentShaderBuilder::declareCustomOutputColor() {
  auto typeModifier = ShaderVar::TypeModifier::Out;
  auto shaderCaps = programBuilder->getContext()->shaderCaps();
  if ((features & PrivateFeature::FramebufferFetch) &&
      shaderCaps->frameBufferFetchNeedsCustomOutput &&
      !shaderCaps->frameBufferFetchUsesSubpassInput) {
    typeModifier = ShaderVar::TypeModifier::InOut;
  }

  outputs.emplace_back(CUSTOM_COLOR_OUTPUT_NAME, SLType::Float4, typeModifier);
}

std::string FragmentShaderBuilder::clampToSubsets(const std::string& coordName,
                                                  const std::string& extraSubset,
                                                  const std::string& subset) {
  if (extraSubset.empty() && subset.empty()) {
    return coordName;
  }
  const std::string out = "clampedCoord" + std::to_string(clampedCoordIndex++);
  recordClampSubsetsIR(coordName, extraSubset, subset, out);
  return out;
}

TextureReadResult FragmentShaderBuilder::readTexture(
    SamplerHandle handle, const std::string& coordName, const std::string& strictSubset,
    const std::string& drawSubset, bool* recorded, ProgramIRBuilder* irBuilder,
    ValueID samplerValue, SLType coordType, ValueID strictSubsetValue, ValueID drawSubsetValue,
    bool coverageSample) {
  if (recorded != nullptr) {
    *recorded = false;
  }
  auto uniformHandler = programBuilder->uniformHandler();
  auto sampler = uniformHandler->getSamplerVariable(handle);
  const int swizzleCode = ReadSwizzleCode(uniformHandler->getSamplerSwizzle(handle));
  const std::string temp = "tgfxRead" + std::to_string(tempCallIndex++);
  const bool clampInside = !strictSubset.empty() || !drawSubset.empty();
  TextureReadResult result = {temp, INVALID_VALUE_ID};
  if (irBuilder == nullptr || samplerValue == INVALID_VALUE_ID || coordType == SLType::Void) {
    return result;
  }
  // Only the 2D reader carries the subsets as arguments; the rect and external readers apply them
  // as a coordinate clamp, so the clamped value has to exist in the IR before the sample reads it.
  const bool carriesSubset = sampler.type() == SLType::Texture2DSampler;
  const std::string sampleCoord =
      carriesSubset ? coordName : clampToSubsets(coordName, strictSubset, drawSubset);
  auto coordKind = IRValueKind::Local;
  for (const auto& varying : irBuilder->IR().interface.varyings) {
    if (varying.name == sampleCoord) {
      coordKind = IRValueKind::Varying;
      break;
    }
  }
  const auto coordValue =
      irBuilder->InternValue(sampleCoord, coordType, Precision::High, coordKind);
  if (coordValue != INVALID_VALUE_ID) {
    result.value = irBuilder->CreateTextureSample(
        samplerValue, coordValue, SLType::Float4, temp, sampler.type(), swizzleCode, clampInside,
        strictSubsetValue, drawSubsetValue, coverageSample);
  }
  if (recorded != nullptr) {
    *recorded = result.value != INVALID_VALUE_ID;
  }
  return result;
}

void FragmentShaderBuilder::onBeforeChildProcEmitCode(const FragmentProcessor* child) const {
  programBuilder->currentProcessors.push_back(child);
}

void FragmentShaderBuilder::onAfterChildProcEmitCode() const {
  programBuilder->currentProcessors.pop_back();
}

std::string FragmentShaderBuilder::emitPerspTextCoord(const CoordValue& coordVar) {
  if (coordVar.type() == SLType::Float2) {
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::ReadVarying;
    instruction.src = coordVar.name();
    instruction.varying = coordVar.varying;
    recordFragmentInstruction(std::move(instruction));
    return coordVar.name();
  }
  if (coordVar.type() == SLType::Float3) {
    const std::string perspCoordName = "persp_" + coordVar.name();
    // The IR definition of the projection: without it a sampler that consumes this coordinate
    // registers an undefined value (the class of unresolved-name failures).
    {
      auto& ir = programBuilder->programIRBuilder();
      const auto sourceValue =
          ir.InternValue(coordVar.name(), SLType::Float3, Precision::High, IRValueKind::Varying);
      if (sourceValue != INVALID_VALUE_ID) {
        const auto xy = ir.CreateSwizzle(sourceValue, SLType::Float2, "xy");
        const auto z = ir.CreateSwizzle(sourceValue, SLType::Float, "z");
        const auto projected = ir.CreateBinary(IRBinaryOp::Div, SLType::Float2, xy, z);
        const auto perspValue =
            ir.InternValue(perspCoordName, SLType::Float2, Precision::High, IRValueKind::Local);
        if (perspValue != INVALID_VALUE_ID) {
          ir.Assign({perspValue, ""}, projected);
        }
      }
    }
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::Project;
    instruction.output = perspCoordName;
    instruction.src = coordVar.name();
    instruction.varying = coordVar.varying;
    recordFragmentInstruction(std::move(instruction));
    return perspCoordName;
  }
  DEBUG_ASSERT(false);
  return "";
}
}  // namespace tgfx
