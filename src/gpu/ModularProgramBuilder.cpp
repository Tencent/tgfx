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
                                                       CoordTransformFunc coordTransformFunc,
                                                       bool skipSamplerCollection) {
  // Use ProcessorGuard to push this processor for correct name mangling.
  currentProcessors.push_back(processor);
  std::string output;
  nameExpression(&output, "output");

  auto processorIndex = programInfo->getProcessorIndex(processor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       processor->name().c_str());

  // Collect texture samplers from this FP's entire subtree FIRST (before any dispatch).
  // Skip when called recursively from a container FP (parent already collected all samplers).
  if (!skipSamplerCollection) {
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
  }

  // Container FPs currently fall through to the legacy emitContainerCode() path below.
  // The modular container path (emitModularContainerFP) is disabled because it causes
  // GLSL macro/function-signature conflicts when multiple FPs of the same type exist
  // in the same shader program. Each .glsl module can only be included once with one
  // set of macro values, but different FP instances may need different macro values.
  // TODO: Re-enable once per-instance function specialization is supported.

  // Try legacy container dispatch via emitContainerCode().
  // The emitChild callback now recursively calls emitModularFragProc for modular children.
  FragmentProcessor::TransformedCoordVars rootCoords(
      processor, transformedCoordVarsIdx < transformedCoordVars.size()
                     ? &transformedCoordVars[transformedCoordVarsIdx]
                     : nullptr);
  FragmentProcessor::TextureSamplers rootSamplers(
      processor, currentTexSamplers.empty() ? nullptr : &currentTexSamplers[0]);
  auto emitChildCallback = [this, processor, &rootCoords, &rootSamplers](
                               const FragmentProcessor* child, size_t /*childCoordIdx*/,
                               const std::string& childInput,
                               CoordTransformFunc coordFunc) -> std::string {
    // Find child index within the container processor.
    size_t childIndex = 0;
    for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
      if (processor->childProcessor(i) == child) {
        childIndex = i;
        break;
      }
    }

    // Legacy path for all children inside emitContainerCode(): use EmitArgs + emitCode.
    // We don't use the modular path here because the parent container already included its
    // own .glsl module, and the child's module may conflict (same module with different macros).
    auto fragBuilder = fragmentShaderBuilder();
    fragBuilder->onBeforeChildProcEmitCode(child);
    std::string inputName;
    if (!childInput.empty() && childInput != "vec4(1.0)") {
      inputName = "_childInput";
      inputName += programInfo->getMangledSuffix(child);
      fragBuilder->codeAppendf("vec4 %s = %s;", inputName.c_str(), childInput.c_str());
    }
    std::string childOutput;
    currentProcessors.push_back(child);
    nameExpression(&childOutput, "output");
    fragBuilder->codeAppend("{\n");
    fragBuilder->codeAppendf("// Processor%d : %s\n", programInfo->getProcessorIndex(child),
                             child->name().c_str());
    FragmentProcessor::TransformedCoordVars coordVars = rootCoords.childInputs(childIndex);
    FragmentProcessor::TextureSamplers texSamplers = rootSamplers.childInputs(childIndex);
    std::function<std::string(std::string_view)> legacyCoordFunc;
    if (coordFunc) {
      legacyCoordFunc = [cf = std::move(coordFunc)](std::string_view sv) -> std::string {
        return cf(std::string(sv));
      };
    }
    FragmentProcessor::EmitArgs childArgs(
        fragBuilder, uniformHandler(), childOutput, inputName.empty() ? "vec4(1.0)" : inputName,
        subsetVarName, &coordVars, &texSamplers, std::move(legacyCoordFunc));
    child->emitCode(childArgs);
    fragBuilder->codeAppend("}\n");
    fragBuilder->onAfterChildProcEmitCode();
    currentProcessors.pop_back();
    return childOutput;
  };
  if (processor->emitContainerCode(fragmentShaderBuilder(), uniformHandler(), input, output,
                                   transformedCoordVarsIdx, emitChildCallback)) {
    fragmentShaderBuilder()->codeAppend("}");
    currentProcessors.pop_back();
    return output;
  }

  // Leaf FP: try modular path first. If the module was already included by another FP instance,
  // fall back to legacy emitCode() to avoid macro/signature conflicts.
  auto leafName = processor->name();
  bool canUseModular = ShaderModuleRegistry::HasModule(leafName) &&
                       includedModules.count(ShaderModuleRegistry::GetModuleID(leafName)) == 0;
  if (canUseModular) {
    emitLeafFPCall(processor, transformedCoordVarsIdx, input, output, coordTransformFunc);
  } else {
    // Legacy fallback: build EmitArgs and call emitCode().
    FragmentProcessor::TransformedCoordVars leafCoords(
        processor, transformedCoordVarsIdx < transformedCoordVars.size()
                       ? &transformedCoordVars[transformedCoordVarsIdx]
                       : nullptr);
    FragmentProcessor::TextureSamplers leafSamplers(
        processor, currentTexSamplers.empty() ? nullptr : &currentTexSamplers[0]);
    std::string inputName;
    if (!input.empty() && input != "vec4(1.0)") {
      inputName = "_leafInput";
      inputName += programInfo->getMangledSuffix(processor);
      fragmentShaderBuilder()->codeAppendf("vec4 %s = %s;", inputName.c_str(), input.c_str());
    }
    FragmentProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), output,
                                     inputName.empty() ? "vec4(1.0)" : inputName, subsetVarName,
                                     &leafCoords, &leafSamplers, {});
    processor->emitCode(args);
  }

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
  return output;
}

// ---- Modular container FP dispatch ----

bool ModularProgramBuilder::emitModularContainerFP(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input,
                                                   const std::string& output) {
  // Check if the processor implements the new modular container interface.
  if (processor->numChildProcessors() == 0) {
    return false;
  }
  auto funcFile = processor->shaderFunctionFile();
  if (funcFile.empty() || !ShaderModuleRegistry::HasModule(processor->name())) {
    return false;
  }
  // If this module was already included (another processor of the same type exists in this shader),
  // fall back to legacy path to avoid macro redefinition errors in GLSL.
  auto moduleID = ShaderModuleRegistry::GetModuleID(processor->name());
  if (includedModules.count(moduleID) > 0) {
    return false;
  }
  // Probe: try building with dummy args. If the result is empty, this container
  // doesn't support the modular path yet.
  std::vector<std::string> dummyChildren(processor->numChildProcessors(), "vec4(0.0)");
  MangledUniforms dummyUniforms;
  MangledSamplers dummySamplers;
  MangledVaryings dummyVaryings;
  auto probeResult =
      processor->buildContainerCallStatement(input.empty() ? "vec4(1.0)" : input, dummyChildren,
                                             dummyUniforms, dummySamplers, dummyVaryings);
  if (probeResult.statement.empty()) {
    return false;
  }

  // This container supports the modular path. Recursively emit all children.
  auto plan = processor->getChildEmitPlan(input.empty() ? "vec4(1.0)" : input);
  std::vector<std::string> childOutputs(processor->numChildProcessors());
  for (const auto& info : plan) {
    auto childCoordOffset =
        processor->computeChildCoordOffset(transformedCoordVarsIdx, info.childIndex);
    std::string childInput;
    if (info.useOutputOfChild >= 0) {
      // Use the output of a previously-emitted child as input.
      childInput = childOutputs[static_cast<size_t>(info.useOutputOfChild)];
    } else if (!info.inputOverride.empty()) {
      // Use a static expression as input override.
      std::string overrideName = "_containerChildInput_" + std::to_string(info.childIndex);
      fragmentShaderBuilder()->codeAppendf("vec4 %s = %s;", overrideName.c_str(),
                                           info.inputOverride.c_str());
      childInput = overrideName;
    } else {
      childInput = input.empty() ? std::string("vec4(1.0)") : input;
    }
    childOutputs[info.childIndex] = emitModularFragProc(processor->childProcessor(info.childIndex),
                                                        childCoordOffset, childInput, {}, true);
  }

  // Declare container's own uniforms.
  FPResources resources;
  processor->declareResources(uniformHandler(), resources.uniforms, resources.samplers);

  // Populate sampler names from the already-collected currentTexSamplers.
  for (size_t i = 0; i < currentTexSamplers.size(); ++i) {
    auto samplerVar = uniformHandler()->getSamplerVariable(currentTexSamplers[i]);
    resources.samplers.add("TextureSampler_" + std::to_string(i), samplerVar.name());
  }
  // Populate coord transform varying.
  if (transformedCoordVarsIdx < transformedCoordVars.size()) {
    auto texCoordName =
        fragmentShaderBuilder()->emitPerspTextCoord(transformedCoordVars[transformedCoordVarsIdx]);
    resources.varyings.addCoordTransform(0, texCoordName);
  }
  if (!subsetVarName.empty()) {
    resources.varyings.add("subsetVar", subsetVarName);
  }

  // Emit macros and include module.
  emitProcessorDefines(processor);
  includeModule(ShaderModuleRegistry::GetModuleID(processor->name()));

  // Build and emit the container function call.
  auto result = processor->buildContainerCallStatement(input.empty() ? "vec4(1.0)" : input,
                                                       childOutputs, resources.uniforms,
                                                       resources.samplers, resources.varyings);
  // Write preamble (#define macros) to Definitions section before the function call.
  if (!result.preamble.empty()) {
    fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += result.preamble;
  }
  fragmentShaderBuilder()->codeAppend(result.statement);
  if (result.outputVarName != output) {
    fragmentShaderBuilder()->codeAppendf("%s = %s;", output.c_str(), result.outputVarName.c_str());
  }
  return true;
}

// ---- Leaf FP function call emission ----

void ModularProgramBuilder::emitLeafFPCall(const FragmentProcessor* processor,
                                           size_t transformedCoordVarsIdx, const std::string& input,
                                           const std::string& output,
                                           const CoordTransformFunc& coordTransformFunc) {
  auto name = processor->name();
  DEBUG_ASSERT(ShaderModuleRegistry::HasModule(name));

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

  // All 10 GP types support modular FS path via buildColorCallExpr/buildCoverageCallExpr.
  bool useModularFS = true;

  if (useModularFS) {
    // Emit GP shader macros to both VS and FS definitions BEFORE emitCode,
    // so that .vert.glsl functions injected by emitCode can see the #define directives.
    ShaderMacroSet macros;
    geometryProcessor->onBuildShaderMacros(macros);
    if (!macros.empty()) {
      auto preamble = macros.toPreamble();
      fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += preamble;
      vertexShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += preamble;
    }

    // Modular path: call emitCode with skipFragmentCode=true and skipVertexCode=true.
    MangledVaryings gpVaryings;
    MangledUniforms gpUniforms;
    GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(),
                                     varyingHandler(), uniformHandler(), getContext()->shaderCaps(),
                                     *outputColor, *outputCoverage, &transformHandler,
                                     &subsetVarName, true);
    args.skipVertexCode = true;
    args.gpVaryings = &gpVaryings;
    args.gpUniforms = &gpUniforms;
    geometryProcessor->emitCode(args);

    // Generate FS function calls.
    auto colorResult = geometryProcessor->buildColorCallExpr(gpUniforms, gpVaryings);
    auto coverageResult = geometryProcessor->buildCoverageCallExpr(gpUniforms, gpVaryings);
    fragmentShaderBuilder()->codeAppend(colorResult.statement);
    fragmentShaderBuilder()->codeAppendf("%s = %s;\n", outputColor->c_str(),
                                         colorResult.outputVarName.c_str());
    fragmentShaderBuilder()->codeAppend(coverageResult.statement);
    fragmentShaderBuilder()->codeAppendf("%s = %s;\n", outputCoverage->c_str(),
                                         coverageResult.outputVarName.c_str());
  } else {
    // Legacy path: call emitCode with full VS+FS generation.
    GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(),
                                     varyingHandler(), uniformHandler(), getContext()->shaderCaps(),
                                     *outputColor, *outputCoverage, &transformHandler,
                                     &subsetVarName);
    geometryProcessor->emitCode(args);
  }

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

  std::string inputColor = !colorIn.empty() ? colorIn : "vec4(1.0)";
  std::string inputCoverage = !coverageIn.empty() ? coverageIn : "vec4(1.0)";
  auto outputColor = fragmentShaderBuilder()->colorOutputName();

  // Emit XP shader macros.
  ShaderMacroSet macros;
  xferProcessor->onBuildShaderMacros(macros);
  if (!macros.empty()) {
    fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += macros.toPreamble();
  }

  // Register dst texture sampler and uniforms.
  MangledUniforms uniforms;
  MangledSamplers samplers;
  SamplerHandle dstTextureSamplerHandle;
  bool hasDstTexture = xferProcessor->dstTextureView() != nullptr;
  if (hasDstTexture) {
    dstTextureSamplerHandle =
        emitSampler(xferProcessor->dstTextureView()->getTexture(), "DstTextureSampler");
    auto samplerVar = uniformHandler()->getSamplerVariable(dstTextureSamplerHandle);
    samplers.add("DstTextureSampler", samplerVar.name());
    auto topLeftName = uniformHandler()->addUniform("DstTextureUpperLeft", UniformFormat::Float2,
                                                    ShaderStage::Fragment);
    uniforms.add("DstTextureUpperLeft", topLeftName);
    auto scaleName = uniformHandler()->addUniform("DstTextureCoordScale", UniformFormat::Float2,
                                                  ShaderStage::Fragment);
    uniforms.add("DstTextureCoordScale", scaleName);
  }

  // Get dst color expression for non-texture-read path.
  std::string dstColorExpr;
  if (!hasDstTexture) {
    dstColorExpr = fragmentShaderBuilder()->dstColor();
  }

  auto result = xferProcessor->buildXferCallStatement(inputColor, inputCoverage, outputColor,
                                                      dstColorExpr, uniforms, samplers);
  if (!result.statement.empty()) {
    // Include XP module and blend mode utility functions.
    includeModule(ShaderModuleID::BlendModes);
    auto xpName = xferProcessor->name();
    if (ShaderModuleRegistry::HasModule(xpName)) {
      includeModule(ShaderModuleRegistry::GetModuleID(xpName));
    }
    fragmentShaderBuilder()->codeAppend(result.statement);
  } else {
    // Fallback to legacy emitCode().
    XferProcessor::EmitArgs args(fragmentShaderBuilder(), uniformHandler(), inputColor,
                                 inputCoverage, outputColor, dstTextureSamplerHandle);
    xferProcessor->emitCode(args);
  }

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
}

}  // namespace tgfx
