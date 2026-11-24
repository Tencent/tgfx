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

#include "ProgramBuilder.h"
#include "core/utils/Log.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XferProcessor.h"

namespace tgfx {
class ProcessorGuard {
 public:
  ProcessorGuard(ProgramBuilder* builder, const Processor* processor) : builder(builder) {
    builder->currentProcessors.push_back(processor);
  }

  ~ProcessorGuard() {
    builder->currentProcessors.pop_back();
  }

 private:
  ProgramBuilder* builder = nullptr;
};

ProgramBuilder::ProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : context(context), programInfo(programInfo) {
}

bool ProgramBuilder::emitAndInstallProcessors() {
  std::string inputColor;
  std::string inputCoverage;
  emitAndInstallGeoProc(&inputColor, &inputCoverage);
  emitAndInstallFragProcessors(&inputColor, &inputCoverage);
  emitAndInstallXferProc(inputColor, inputCoverage);
  emitFSOutputSwizzle();

  if (programIR.Failed()) {
    LOGE("ProgramBuilder: %s", programIR.Failure().c_str());
    return false;
  }
  std::string validationError;
  if (!ValidateProgramIR(programIR.IR(), &validationError)) {
    LOGE("ProgramBuilder: %s", validationError.c_str());
    return false;
  }
  return checkSamplerCounts();
}

void ProgramBuilder::emitAndInstallGeoProc(std::string* outputColor, std::string* outputCoverage) {
  // We don't want the RTAdjustName to be mangled, so we add it to the uniform handler before the
  // processor guard.
  uniformHandler()->addUniform(RTAdjustName, UniformFormat::Float4, ShaderStage::Vertex);
  auto geometryProcessor = programInfo->getGeometryProcessor();
  // Set the current processor so that all variable names will be mangled correctly.
  ProcessorGuard processorGuard(this, geometryProcessor);
  nameExpression(outputColor, "outputColor");
  nameExpression(outputCoverage, "outputCoverage");

  auto processorIndex = programInfo->getProcessorIndex(geometryProcessor);
  // Enclose custom code in a block to avoid namespace conflicts
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       geometryProcessor->name().c_str());
  vertexShaderBuilder()->codeAppendf("// Processor%d : %s\n", processorIndex,
                                     geometryProcessor->name().c_str());

  for (const auto& attribute : geometryProcessor->vertexAttributes()) {
    programIRBuilder().AddAttribute(
        {ShaderVar(attribute).name(), ShaderVar(attribute).type(), Precision::High, 0});
  }
  GeometryProcessor::FPCoordTransformHandler transformHandler(programInfo, &transformedCoordVars);
  GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(), varyingHandler(),
                                   uniformHandler(), getContext()->shaderCaps(), *outputColor,
                                   *outputCoverage, &transformHandler, &subsetVarName,
                                   &programIRBuilder());
  programIRBuilder().SetStage(ShaderStage::Vertex);
  geometryProcessor->emitCode(args);
  programIRBuilder().SetStage(ShaderStage::Fragment);
  fragmentShaderBuilder()->codeAppend("}");
}

void ProgramBuilder::emitAndInstallFragProcessors(std::string* color, std::string* coverage) {
  size_t transformedCoordVarsIdx = 0;
  std::string** inOut = &color;
  for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
    if (i == programInfo->numColorFragmentProcessors()) {
      inOut = &coverage;
    }
    const auto fp = programInfo->getFragmentProcessor(i);
    auto output = emitAndInstallFragProc(fp, transformedCoordVarsIdx, **inOut);
    FragmentProcessor::Iter iter(fp);
    while (const FragmentProcessor* tempFP = iter.next()) {
      transformedCoordVarsIdx += tempFP->numCoordTransforms();
    }
    **inOut = output;
  }
}

template <typename T>
static const T* GetPointer(const std::vector<T>& vector, size_t atIndex) {
  if (atIndex >= vector.size()) {
    return nullptr;
  }
  return &vector[atIndex];
}

std::string ProgramBuilder::emitAndInstallFragProc(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input) {
  ProcessorGuard processorGuard(this, processor);
  std::string output;
  nameExpression(&output, "output");

  // Enclose custom code in a block to avoid namespace conflicts
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n",
                                       programInfo->getProcessorIndex(processor),
                                       processor->name().c_str());

  std::vector<SamplerHandle> texSamplers;
  FragmentProcessor::Iter fpIter(processor);
  int samplerIndex = 0;
  while (const auto subFP = fpIter.next()) {
    for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
      std::string name = "TextureSampler_";
      name += std::to_string(samplerIndex++);
      auto texture = subFP->textureAt(i);
      texSamplers.emplace_back(emitSampler(texture, name));
    }
  }
  FragmentProcessor::TransformedCoordVars coords(
      processor, GetPointer(transformedCoordVars, transformedCoordVarsIdx));
  FragmentProcessor::TextureSamplers textureSamplers(processor, GetPointer(texSamplers, 0));
  const auto inputValue =
      programIR.InternValue(input, SLType::Float4, Precision::High, IRValueKind::Local);
  const auto outputValue =
      programIR.InternValue(output, SLType::Float4, Precision::High, IRValueKind::Local);
  FragmentProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), output, input,
                                   subsetVarName, &coords, &textureSamplers, {}, &programIR,
                                   inputValue, outputValue, &samplerValues);

  processor->emitCode(args);
  fragmentShaderBuilder()->codeAppend("}");
  return output;
}

void ProgramBuilder::emitAndInstallXferProc(const std::string& colorIn,
                                            const std::string& coverageIn) {
  auto xferProcessor = programInfo->getXferProcessor();
  ProcessorGuard processorGuard(this, xferProcessor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n",
                                       programInfo->getProcessorIndex(xferProcessor),
                                       xferProcessor->name().c_str());

  SamplerHandle dstTextureSamplerHandle;
  if (auto dstTextureView = xferProcessor->dstTextureView()) {
    dstTextureSamplerHandle = emitSampler(dstTextureView->getTexture(), "DstTextureSampler");
  }

  std::string inputColor = !colorIn.empty() ? colorIn : "vec4(1.0)";
  std::string inputCoverage = !coverageIn.empty() ? coverageIn : "vec4(1.0)";
  XferProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), inputColor, inputCoverage,
                               fragmentShaderBuilder()->colorOutputName(), dstTextureSamplerHandle);
  xferProcessor->emitCode(args);
  bool hasOutput = false;
  for (const auto& statement : programIRBuilder().IR().statements) {
    if (statement.kind == IRStatementKind::StoreOutput) {
      hasOutput = true;
      break;
    }
  }
  if (!hasOutput) {
    ValueID source = INVALID_VALUE_ID;
    for (const auto& value : programIRBuilder().IR().values) {
      if (value.debugName == inputColor) {
        source = value.id;
        break;
      }
    }
    if (source == INVALID_VALUE_ID && (inputColor == "vec4(1.0)" || inputColor == "1.0")) {
      const auto white = programIRBuilder().CreateConstant(1.0f);
      source = programIRBuilder().CreateConstruct(SLType::Float4, {white, white, white, white});
    } else if (source == INVALID_VALUE_ID && (inputColor == "vec4(0.0)" || inputColor == "0.0")) {
      const auto black = programIRBuilder().CreateConstant(0.0f);
      source = programIRBuilder().CreateConstruct(SLType::Float4, {black, black, black, black});
    }
    if (source != INVALID_VALUE_ID) {
      const auto output = programIRBuilder().CreateValue(SLType::Float4, Precision::High,
                                                         IRValueKind::Local, "fragmentOutput");
      programIRBuilder().StoreOutput(output, source);
    }
  }
  fragmentShaderBuilder()->codeAppend("}");
}

SamplerHandle ProgramBuilder::emitSampler(std::shared_ptr<Texture> texture,
                                          const std::string& name) {
  ++numFragmentSamplers;
  const auto handle = uniformHandler()->addSampler(texture, name);
  const auto samplerVar = uniformHandler()->getSamplerVariable(handle);
  const auto value =
      programIR.AddSampler({samplerVar.name(), samplerVar.type(), static_cast<uint32_t>(0)});
  if (samplerValues.size() <= handle.toIndex()) {
    samplerValues.resize(handle.toIndex() + 1, INVALID_VALUE_ID);
  }
  samplerValues[handle.toIndex()] = value;
  return handle;
}

void ProgramBuilder::emitFSOutputSwizzle() {
  // Swizzle the fragment shader outputs if necessary.
  auto swizzle = programInfo->getOutputSwizzle();
  if (swizzle == Swizzle::RGBA()) {
    return;
  }
  auto fragBuilder = fragmentShaderBuilder();
  const auto& output = fragBuilder->colorOutputName();
  fragBuilder->codeAppendf("%s = %s.%s;", output.c_str(), output.c_str(), swizzle.c_str());
}

std::string ProgramBuilder::nameVariable(const std::string& name) const {
  auto processor = currentProcessors.empty() ? nullptr : currentProcessors.back();
  if (processor == nullptr) {
    return name;
  }
  return name + programInfo->getMangledSuffix(processor);
}

void ProgramBuilder::nameExpression(std::string* output, const std::string& baseName) {
  // Create var to hold the stage result. If we already have a valid output name, just use that
  // otherwise create a new mangled one. This name is only valid if we are reordering stages
  // and have to tell stage exactly where to put its output.
  std::string outName;
  if (!output->empty()) {
    outName = *output;
  } else {
    outName = nameVariable(baseName);
  }
  // Coverage is a geometric quantity (AA edge / clip / mask) that is sensitive to precision; a
  // mediump carrier can collapse interior coverage to zero on some ES GPUs. Declare the coverage
  // stage result as highp while keeping color at the default precision.
  if (getContext()->shaderCaps()->usesPrecisionModifiers && baseName == "outputCoverage") {
    fragmentShaderBuilder()->codeAppendf("highp vec4 %s;", outName.c_str());
  } else {
    fragmentShaderBuilder()->codeAppendf("vec4 %s;", outName.c_str());
  }
  *output = outName;
}

void ProgramBuilder::finalizeShaders() {
  vertexShaderBuilder()->finalize();
  fragmentShaderBuilder()->finalize();
}
}  // namespace tgfx
