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
  if (fragmentProgramRecorder == nullptr) {
    return ShaderBuilder::clampToSubsets(coordName, extraSubset, subset);
  }
  if (extraSubset.empty() && subset.empty()) {
    return coordName;
  }
  auto entry = appendFunction("tgfxClampSubset");
  if (entry.empty()) {
    return coordName;
  }
  const std::string out = "clampedCoord" + std::to_string(clampedCoordIndex++);
  aliasValue(out, coordName, "highp vec2");
  if (!extraSubset.empty()) {
    appendCall(out, entry, {out, extraSubset});
  }
  if (!subset.empty()) {
    appendCall(out, entry, {out, subset});
  }
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
  const char* reader = TextureReaderName(sampler.type());
  const bool carriesSubset = sampler.type() == SLType::Texture2DSampler;
  const int swizzleCode =
      carriesSubset ? ReadSwizzleCode(uniformHandler->getSamplerSwizzle(handle)) : -1;
  auto entry = reader == nullptr ? std::string() : appendFunction(reader);
  const std::string temp = "tgfxRead" + std::to_string(tempCallIndex++);
  const bool clampInside = !strictSubset.empty() || !drawSubset.empty();
  // The 2D reader carries one subset clamp as an argument, so a single clamp rides the call. Two
  // clamps, or a reader that takes none (rect/external) or a channel swizzle the function does not
  // carry, keep the clamp at the call site: dropping it would sample outside the source rect.
  const bool twoClamps = !strictSubset.empty() && !drawSubset.empty();
  if (!entry.empty() && carriesSubset && swizzleCode >= 0 && !twoClamps) {
    codeAppendf("vec4 %s;", temp.c_str());
    const std::string subset =
        clampInside ? (drawSubset.empty() ? strictSubset : drawSubset) : std::string("vec4(0.0)");
    const std::string clampOn = clampInside ? "1" : "0";
    appendCall(temp, entry,
               {sampler.name(), coordName, subset, clampOn, std::to_string(swizzleCode)});
    if (recorded != nullptr) {
      *recorded = true;
    }
    TextureReadResult result = {temp, INVALID_VALUE_ID};
    if (irBuilder != nullptr && samplerValue != INVALID_VALUE_ID && coordType != SLType::Void) {
      auto coordKind = IRValueKind::Local;
      for (const auto& varying : irBuilder->IR().interface.varyings) {
        if (varying.name == coordName) {
          coordKind = IRValueKind::Varying;
          break;
        }
      }
      const auto coordValue =
          irBuilder->InternValue(coordName, coordType, Precision::High, coordKind);
      if (coordValue != INVALID_VALUE_ID) {
        result.value = irBuilder->CreateTextureSample(
            samplerValue, coordValue, SLType::Float4, temp, sampler.type(), swizzleCode,
            clampInside, strictSubsetValue, drawSubsetValue, coverageSample);
      }
    }
    return result;
  }
  const auto finalCoord = clampToSubsets(coordName, strictSubset, drawSubset);
  aliasValue(temp, textureLookupExpression(handle, finalCoord), "vec4");
  TextureReadResult result = {temp, INVALID_VALUE_ID};
  if (irBuilder != nullptr && samplerValue != INVALID_VALUE_ID && coordType != SLType::Void) {
    auto coordKind = IRValueKind::Local;
    for (const auto& varying : irBuilder->IR().interface.varyings) {
      if (varying.name == finalCoord) {
        coordKind = IRValueKind::Varying;
        break;
      }
    }
    const auto coordValue =
        irBuilder->InternValue(finalCoord, coordType, Precision::High, coordKind);
    if (coordValue != INVALID_VALUE_ID) {
      result.value = irBuilder->CreateTextureSample(
          samplerValue, coordValue, SLType::Float4, temp, sampler.type(), swizzleCode, clampInside,
          strictSubsetValue, drawSubsetValue, coverageSample);
    }
  }
  return result;
}

void FragmentShaderBuilder::recordStageRoot(const std::string& outputVar) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Root;
    record.dst = outputVar;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::Root;
  instruction.output = outputVar;
  recordFragmentInstruction(std::move(instruction));
}

void FragmentShaderBuilder::aliasValue(const std::string& dstName, const std::string& srcName,
                                       const std::string& declaration) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Alias;
    record.dst = dstName;
    record.src = srcName;
    record.declaration = declaration;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  if (!declaration.empty()) {
    codeAppendf("%s ", declaration.c_str());
  }
  codeAppendf("%s = %s;", dstName.c_str(), srcName.c_str());
  setRawCaptureEnabled(true);
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::Alias;
  instruction.output = dstName;
  instruction.src = srcName;
  instruction.declaration = declaration;
  instruction.text = codeTextFrom(start);
  recordFragmentInstruction(std::move(instruction));
}

void FragmentShaderBuilder::copyValue(const std::string& dstName, const std::string& srcName) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Alias;
    record.dst = dstName;
    record.src = srcName;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  codeAppendf("%s = %s;", dstName.c_str(), srcName.c_str());
  setRawCaptureEnabled(true);
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::Alias;
  instruction.output = dstName;
  instruction.src = srcName;
  instruction.text = codeTextFrom(start);
  recordFragmentInstruction(std::move(instruction));
}

void FragmentShaderBuilder::storeColor(const std::string& outputVar, const std::string& srcName) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Color;
    record.dst = outputVar;
    record.src = srcName;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  codeAppendf("%s = %s;", outputVar.c_str(), srcName.c_str());
  setRawCaptureEnabled(true);
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::StoreColor;
  instruction.output = outputVar;
  instruction.src = srcName;
  instruction.text = codeTextFrom(start);
  recordFragmentInstruction(std::move(instruction));
}

void FragmentShaderBuilder::storeCoverage(const std::string& outputVar, const std::string& srcName,
                                          bool splat) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Coverage;
    record.dst = outputVar;
    record.src = srcName;
    record.splat = splat;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  if (splat) {
    codeAppendf("%s = vec4(%s);", outputVar.c_str(), srcName.c_str());
  } else {
    codeAppendf("%s = %s;", outputVar.c_str(), srcName.c_str());
  }
  setRawCaptureEnabled(true);
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::StoreCoverage;
  instruction.output = outputVar;
  instruction.src = srcName;
  instruction.splat = splat;
  instruction.text = codeTextFrom(start);
  recordFragmentInstruction(std::move(instruction));
}

void FragmentShaderBuilder::combineColorWithCoverage(const std::string& outputVar,
                                                     const std::string& colorVar,
                                                     const std::string& coverageVar) {
  if (sourceRecorder != nullptr) {
    FragmentSourceRecord record = {};
    record.kind = FragmentSourceRecord::Kind::Combine;
    record.dst = outputVar;
    record.src = colorVar;
    record.operand = coverageVar;
    record.order = nextEmitOrder();
    sourceRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  codeAppendf("%s = %s * %s;", outputVar.c_str(), colorVar.c_str(), coverageVar.c_str());
  setRawCaptureEnabled(true);
  FragmentInstruction instruction = {};
  instruction.kind = FragmentInstructionKind::Combine;
  instruction.output = outputVar;
  instruction.src = colorVar;
  instruction.operand = coverageVar;
  instruction.text = codeTextFrom(start);
  recordFragmentInstruction(std::move(instruction));
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
    if (sourceRecorder != nullptr) {
      FragmentSourceRecord record = {};
      record.kind = FragmentSourceRecord::Kind::Project;
      record.dst = perspCoordName;
      record.src = coordVar.name();
      record.order = nextEmitOrder();
      sourceRecorder->push_back(std::move(record));
    }
    const size_t start = codeTextSize();
    setRawCaptureEnabled(false);
    codeAppendf("highp vec2 %s = %s.xy / %s.z;", perspCoordName.c_str(), coordVar.name().c_str(),
                coordVar.name().c_str());
    setRawCaptureEnabled(true);
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::Project;
    instruction.output = perspCoordName;
    instruction.src = coordVar.name();
    instruction.varying = coordVar.varying;
    instruction.text = codeTextFrom(start);
    recordFragmentInstruction(std::move(instruction));
    return perspCoordName;
  }
  DEBUG_ASSERT(false);
  return "";
}
}  // namespace tgfx
