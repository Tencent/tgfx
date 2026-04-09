/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ModularProgramBuilder.h"
#include "gpu/MangledResources.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

// ---- Public API ----

ModularProgramBuilder::ModularProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : GLSLProgramBuilder(context, programInfo) {
}

bool ModularProgramBuilder::CanUseModularPath(const ProgramInfo* programInfo) {
  // All processors are now supported by the modular builder: leaf FPs via modular .glsl modules,
  // container/complex FPs via legacy emitCode() fallback, and GP/XP via explicit dispatch overrides.
  (void)programInfo;
  return true;
}

bool ModularProgramBuilder::emitAndInstallProcessors() {
  std::string inputColor;
  std::string inputCoverage;
  // GP emission via override with explicit dispatch.
  emitAndInstallGeoProc(&inputColor, &inputCoverage);
  // FP emission via modular path.
  emitModularFragProcessors(&inputColor, &inputCoverage);
  // XP emission via override with explicit dispatch.
  emitAndInstallXferProc(inputColor, inputCoverage);
  emitFSOutputSwizzle();
  return checkSamplerCounts();
}

// ---- Module inclusion ----

void ModularProgramBuilder::includeModule(ShaderModuleID id) {
  if (includedModules.count(id) > 0) {
    return;
  }
  includedModules.insert(id);
  // Always include types first.
  if (id != ShaderModuleID::TypesGLSL && includedModules.count(ShaderModuleID::TypesGLSL) == 0) {
    includeModule(ShaderModuleID::TypesGLSL);
  }
  fragmentShaderBuilder()->addFunction(ShaderModuleRegistry::GetModule(id));
}

// ---- Processor #define emission ----

void ModularProgramBuilder::emitProcessorDefines(const FragmentProcessor* processor) {
  ShaderMacroSet macros;
  processor->onBuildShaderMacros(macros);
  if (!macros.empty()) {
    fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += macros.toPreamble();
  }
}

// ---- FP emission ----

void ModularProgramBuilder::emitModularFragProcessors(std::string* color, std::string* coverage) {
  size_t transformedCoordVarsIdx = 0;
  std::string** inOut = &color;
  for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
    if (i == programInfo->numColorFragmentProcessors()) {
      inOut = &coverage;
    }
    const auto fp = programInfo->getFragmentProcessor(i);
    auto output = emitModularFragProc(fp, transformedCoordVarsIdx, **inOut);
    FragmentProcessor::Iter iter(fp);
    while (const FragmentProcessor* tempFP = iter.next()) {
      transformedCoordVarsIdx += tempFP->numCoordTransforms();
    }
    **inOut = output;
  }
}

std::string ModularProgramBuilder::emitModularFragProc(const FragmentProcessor* processor,
                                                       size_t transformedCoordVarsIdx,
                                                       const std::string& input,
                                                       CoordTransformFunc coordTransformFunc) {
  // Use ProcessorGuard to push this processor for correct name mangling.
  currentProcessors.push_back(processor);
  std::string output;
  nameExpression(&output, "output");

  auto processorIndex = programInfo->getProcessorIndex(processor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       processor->name().c_str());

  // Try polymorphic container dispatch FIRST, before sampler collection.
  // Container FPs that override emitContainerCode() recursively call emitModularFragProc()
  // for each child, and each child collects its own samplers on entry.
  auto emitChildCallback = [this](const FragmentProcessor* child, size_t childCoordIdx,
                                  const std::string& childInput,
                                  CoordTransformFunc coordFunc) -> std::string {
    return emitModularFragProc(child, childCoordIdx, childInput, std::move(coordFunc));
  };
  if (processor->emitContainerCode(fragmentShaderBuilder(), uniformHandler(), input, output,
                                   transformedCoordVarsIdx, emitChildCallback)) {
    fragmentShaderBuilder()->codeAppend("}");
    currentProcessors.pop_back();
    return output;
  }

  // Not a container FP (or container FP without emitContainerCode override).
  // Collect texture samplers from this FP's subtree.
  currentTexSamplers.clear();
  FragmentProcessor::Iter fpIter(processor);
  int samplerIndex = 0;
  while (const auto subFP = fpIter.next()) {
    for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
      std::string samplerName = "TextureSampler_";
      samplerName += std::to_string(samplerIndex++);
      auto texture = subFP->textureAt(i);
      currentTexSamplers.emplace_back(emitSampler(texture, samplerName));
    }
  }

  // Leaf FP: dispatch via buildCallStatement() polymorphic path.
  emitLeafFPCall(processor, transformedCoordVarsIdx, input, output, coordTransformFunc);

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
  return output;
}

// ---- Leaf FP function call emission ----

void ModularProgramBuilder::emitLeafFPCall(const FragmentProcessor* processor,
                                           size_t transformedCoordVarsIdx, const std::string& input,
                                           const std::string& output,
                                           const CoordTransformFunc& coordTransformFunc) {
  auto name = processor->name();
  if (!ShaderModuleRegistry::HasModule(name)) {
    // FP not in module registry — fall back to legacy emitCode() path.
    FragmentProcessor::TransformedCoordVars coords(
        processor, transformedCoordVarsIdx < transformedCoordVars.size()
                       ? &transformedCoordVars[transformedCoordVarsIdx]
                       : nullptr);
    FragmentProcessor::TextureSamplers textureSamplers(
        processor, currentTexSamplers.empty() ? nullptr : &currentTexSamplers[0]);
    FragmentProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), output,
                                     input.empty() ? "vec4(1.0)" : input, subsetVarName, &coords,
                                     &textureSamplers);
    processor->emitCode(args);
    return;
  }

  FPResources resources;
  // Populate coord transform (emit perspective divide if needed).
  if (transformedCoordVarsIdx < transformedCoordVars.size()) {
    auto texCoordName =
        fragmentShaderBuilder()->emitPerspTextCoord(transformedCoordVars[transformedCoordVarsIdx]);
    if (coordTransformFunc) {
      fragmentShaderBuilder()->codeAppendf("highp vec2 transformedCoord = %s;",
                                           coordTransformFunc(texCoordName).c_str());
      texCoordName = "transformedCoord";
    }
    resources.varyings.addCoordTransform(0, texCoordName);
  }
  // Pass the GP subset varying name (used by TextureEffect Strict constraint).
  if (!subsetVarName.empty()) {
    resources.varyings.add("subsetVar", subsetVarName);
  }
  for (size_t i = 0; i < currentTexSamplers.size(); ++i) {
    auto samplerVar = uniformHandler()->getSamplerVariable(currentTexSamplers[i]);
    resources.samplers.add("TextureSampler_" + std::to_string(i), samplerVar.name());
  }
  processor->declareResources(uniformHandler(), resources.uniforms, resources.samplers);
  int fpIndex = programInfo->getProcessorIndex(processor);
  auto result = processor->buildCallStatement(input, fpIndex, resources.uniforms,
                                              resources.varyings, resources.samplers);
  emitProcessorDefines(processor);
  includeModule(ShaderModuleRegistry::GetModuleID(name));
  fragmentShaderBuilder()->codeAppend(result.statement);
  if (result.outputVarName != output) {
    fragmentShaderBuilder()->codeAppendf("%s = %s;", output.c_str(), result.outputVarName.c_str());
  }
}

// ---- GP emission override ----

void ModularProgramBuilder::emitAndInstallGeoProc(std::string* outputColor,
                                                  std::string* outputCoverage) {
  // Add RTAdjust uniform before pushing the processor to avoid name mangling.
  uniformHandler()->addUniform(RTAdjustName, UniformFormat::Float4, ShaderStage::Vertex);
  auto geometryProcessor = programInfo->getGeometryProcessor();
  currentProcessors.push_back(geometryProcessor);
  nameExpression(outputColor, "outputColor");
  nameExpression(outputCoverage, "outputCoverage");

  auto processorIndex = programInfo->getProcessorIndex(geometryProcessor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       geometryProcessor->name().c_str());
  vertexShaderBuilder()->codeAppendf("// Processor%d : %s\n", processorIndex,
                                     geometryProcessor->name().c_str());

  GeometryProcessor::FPCoordTransformHandler transformHandler(programInfo, &transformedCoordVars);
  GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(), varyingHandler(),
                                   uniformHandler(), getContext()->shaderCaps(), *outputColor,
                                   *outputCoverage, &transformHandler, &subsetVarName);

  // Polymorphic dispatch via virtual emitCode().
  geometryProcessor->emitCode(args);

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
}

// ---- XP emission override ----

void ModularProgramBuilder::emitAndInstallXferProc(const std::string& colorIn,
                                                   const std::string& coverageIn) {
  auto xferProcessor = programInfo->getXferProcessor();
  currentProcessors.push_back(xferProcessor);
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

  // Polymorphic dispatch via virtual emitCode().
  xferProcessor->emitCode(args);

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
}

}  // namespace tgfx
