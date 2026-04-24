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
#include "Swizzle.h"
#include "core/utils/Log.h"
#include "gpu/MangledResources.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

// ---- Manifest rendering ----

std::string ModularProgramBuilder::RenderManifest(const ShaderCallManifest& manifest) {
  if (manifest.functionName.empty()) {
    return manifest.statement;
  }
  std::string args;
  for (size_t i = 0; i < manifest.argExpressions.size(); ++i) {
    if (i != 0) {
      args += ", ";
    }
    args += manifest.argExpressions[i];
  }
  if (manifest.declareOutput) {
    return manifest.outputType + " " + manifest.outputVarName + " = " + manifest.functionName +
           "(" + args + ");\n";
  }
  std::string call = manifest.functionName + "(";
  if (!args.empty()) {
    call += args + ", ";
  }
  call += manifest.outputVarName + ");\n";
  return call;
}

// ---- Public API ----

ModularProgramBuilder::ModularProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : GLSLProgramBuilder(context, programInfo) {
}

bool ModularProgramBuilder::CanUseModularPath(const ProgramInfo* programInfo) {
  // All processors are now supported by the modular builder: leaf FPs via modular .glsl modules,
  // All FP dispatching is handled by emitModularFragProc (modular container + leaf paths).
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
  // Output swizzle uses TGFX_OutputSwizzle() from tgfx_fs_boilerplate.glsl. Include it on the
  // non-identity swizzle path so the helper is available when ProgramBuilder::emitFSOutputSwizzle
  // emits the call. Includes are idempotent (no-op if already included for perspective divide).
  if (programInfo->getOutputSwizzle() != Swizzle::RGBA()) {
    includeModule(ShaderModuleID::FSBoilerplate);
  }
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
  // XfermodeEffect depends on BlendModes for tgfx_blend() function.
  if (id == ShaderModuleID::XfermodeEffect) {
    includeModule(ShaderModuleID::BlendModes);
  }
  fragmentShaderBuilder()->addFunction(ShaderModuleRegistry::GetModule(id));
}

std::string ModularProgramBuilder::emitPerspCoordDivide(const ShaderVar& coordVar) {
  if (coordVar.type() == SLType::Float2) {
    return coordVar.name();
  }
  DEBUG_ASSERT(coordVar.type() == SLType::Float3);
  // Inject TGFX_PerspDivide() from tgfx_fs_boilerplate.glsl on first use.
  includeModule(ShaderModuleID::FSBoilerplate);
  const std::string perspCoordName = "perspCoord2D";
  fragmentShaderBuilder()->codeAppendf("highp vec2 %s = TGFX_PerspDivide(%s);",
                                       perspCoordName.c_str(), coordVar.name().c_str());
  return perspCoordName;
}

void ModularProgramBuilder::includeVSModule(ShaderModuleID id) {
  if (includedVSModules.count(id) > 0) {
    return;
  }
  includedVSModules.insert(id);
  // All GP vertex modules depend on TGFX_NormalizePosition() from tgfx_vs_boilerplate.glsl.
  // Include it transitively exactly once.
  if (id != ShaderModuleID::VSBoilerplate) {
    includeVSModule(ShaderModuleID::VSBoilerplate);
  }
  vertexShaderBuilder()->addFunction(ShaderModuleRegistry::GetModule(id));
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

std::string ModularProgramBuilder::emitModularFragProc(
    const FragmentProcessor* processor, size_t transformedCoordVarsIdx, const std::string& input,
    CoordTransformFunc coordTransformFunc, bool skipSamplerCollection, size_t samplerOffset) {
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

  // Try modular container path: buildContainerCallStatement().
  // This recursively emits children via emitModularFragProc, then calls the container .glsl.
  if (emitModularContainerFP(processor, transformedCoordVarsIdx, input, output, samplerOffset)) {
    fragmentShaderBuilder()->codeAppend("}");
    currentProcessors.pop_back();
    return output;
  }

  // Leaf FP: dispatch via buildCallStatement() polymorphic path.
  emitLeafFPCall(processor, transformedCoordVarsIdx, input, output, coordTransformFunc,
                 samplerOffset);

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
  return output;
}

// ---- Modular container FP dispatch ----

bool ModularProgramBuilder::emitModularContainerFP(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input,
                                                   const std::string& output,
                                                   size_t samplerOffset) {
  // Check if the processor implements the new modular container interface.
  if (processor->numChildProcessors() == 0) {
    return false;
  }
  // Probe: try building with dummy args to check if this container supports the modular path.
  // Containers without a .glsl module (e.g. ComposeFragmentProcessor) can still work if they
  // provide a valid buildContainerCallStatement() with a non-empty outputVarName.
  std::vector<std::string> dummyChildren(processor->numChildProcessors(), "vec4(0.0)");
  MangledUniforms dummyUniforms;
  MangledSamplers dummySamplers;
  MangledVaryings dummyVaryings;
  auto probeResult =
      processor->buildContainerCallStatement(input.empty() ? "vec4(1.0)" : input, dummyChildren,
                                             dummyUniforms, dummySamplers, dummyVaryings);
  if (probeResult.outputVarName.empty()) {
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
    // Compute child sampler offset within currentTexSamplers.
    size_t childSamplerOffset = samplerOffset;
    FragmentProcessor::Iter childIter(processor);
    while (const auto fp = childIter.next()) {
      if (fp == processor->childProcessor(info.childIndex)) {
        break;
      }
      childSamplerOffset += fp->numTextureSamplers();
    }
    childOutputs[info.childIndex] =
        emitModularFragProc(processor->childProcessor(info.childIndex), childCoordOffset,
                            childInput, {}, true, childSamplerOffset);
  }

  // Declare container's own uniforms.
  FPResources resources;
  processor->declareResources(uniformHandler(), resources.uniforms, resources.samplers);

  // Populate sampler names from currentTexSamplers for this FP's entire subtree.
  // Container FPs may reference child FP samplers in buildContainerCallStatement().
  size_t subtreeSamplers = 0;
  {
    FragmentProcessor::Iter samplerIter(processor);
    while (const auto fp = samplerIter.next()) {
      subtreeSamplers += fp->numTextureSamplers();
    }
  }
  for (size_t i = 0; i < subtreeSamplers; ++i) {
    auto samplerVar = uniformHandler()->getSamplerVariable(currentTexSamplers[samplerOffset + i]);
    resources.samplers.add("TextureSampler_" + std::to_string(i), samplerVar.name());
  }
  // Populate coord transform varying.
  if (transformedCoordVarsIdx < transformedCoordVars.size()) {
    auto texCoordName = emitPerspCoordDivide(transformedCoordVars[transformedCoordVarsIdx]);
    resources.varyings.addCoordTransform(0, texCoordName);
  }
  if (!subsetVarName.empty()) {
    resources.varyings.add("subsetVar", subsetVarName);
  }

  // Emit macros and include module (skip for pure structural containers with no .glsl module).
  emitProcessorDefines(processor);
  if (ShaderModuleRegistry::HasModule(processor->name())) {
    includeModule(ShaderModuleRegistry::GetModuleID(processor->name()));
  }

  // Build and emit the container function call.
  auto result = processor->buildContainerCallStatement(input.empty() ? "vec4(1.0)" : input,
                                                       childOutputs, resources.uniforms,
                                                       resources.samplers, resources.varyings);
  // Write preamble (#define macros) to Definitions section before the function call.
  if (!result.preamble.empty()) {
    fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += result.preamble;
  }
  fragmentShaderBuilder()->codeAppend(RenderManifest(result));
  if (result.outputVarName != output) {
    fragmentShaderBuilder()->codeAppendf("%s = %s;", output.c_str(), result.outputVarName.c_str());
  }
  return true;
}

// ---- Leaf FP function call emission ----

void ModularProgramBuilder::emitLeafFPCall(const FragmentProcessor* processor,
                                           size_t transformedCoordVarsIdx, const std::string& input,
                                           const std::string& output,
                                           const CoordTransformFunc& coordTransformFunc,
                                           size_t samplerOffset) {
  auto name = processor->name();
  DEBUG_ASSERT(ShaderModuleRegistry::HasModule(name));

  FPResources resources;
  // Populate coord transform (emit perspective divide if needed).
  if (transformedCoordVarsIdx < transformedCoordVars.size()) {
    auto texCoordName = emitPerspCoordDivide(transformedCoordVars[transformedCoordVarsIdx]);
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
  // Populate sampler names from currentTexSamplers starting at this FP's offset.
  size_t numSamplers = processor->numTextureSamplers();
  for (size_t i = 0; i < numSamplers; ++i) {
    auto samplerVar = uniformHandler()->getSamplerVariable(currentTexSamplers[samplerOffset + i]);
    resources.samplers.add("TextureSampler_" + std::to_string(i), samplerVar.name());
  }
  processor->declareResources(uniformHandler(), resources.uniforms, resources.samplers);
  int fpIndex = programInfo->getProcessorIndex(processor);
  auto result = processor->buildCallStatement(input, fpIndex, resources.uniforms,
                                              resources.varyings, resources.samplers);
  emitProcessorDefines(processor);
  includeModule(ShaderModuleRegistry::GetModuleID(name));
  fragmentShaderBuilder()->codeAppend(RenderManifest(result));
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

  // Emit GP shader macros to both VS and FS definitions BEFORE emitCode,
  // so that .vert.glsl functions injected by emitCode can see the #define directives.
  ShaderMacroSet macros;
  geometryProcessor->onBuildShaderMacros(macros);
  if (!macros.empty()) {
    auto preamble = macros.toPreamble();
    fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += preamble;
    vertexShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] += preamble;
  }

  MangledVaryings gpVaryings;
  MangledUniforms gpUniforms;
  std::vector<GeometryProcessor::CoordTransformRecord> coordRecords;
  GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(), varyingHandler(),
                                   uniformHandler(), &transformHandler, &subsetVarName);
  args.gpVaryings = &gpVaryings;
  args.gpUniforms = &gpUniforms;
  args.coordTransformRecords = &coordRecords;

  // Phase 1 (resource registration): emitCode should register attributes / uniforms / varyings
  // and call registerCoordTransforms. It must not append any VS code — all VS code emission is
  // now driven by buildVSCallExpr() in phase 2 and emitCoordTransformCode() in phase 3.
  geometryProcessor->emitCode(args);

  // AtlasTextGeometryProcessor samples its atlas during emitCode via the helper function
  // TGFX_AtlasText_SampleAtlas (defined in atlas_text_geometry.frag.glsl). Inject that FS
  // module so the function body is available when the FS is compiled. The set is intentionally
  // hard-coded (same pattern as the GPCoverage whitelist below): AtlasText is currently the
  // only GP that needs an auxiliary FS helper for sampling.
  if (geometryProcessor->name() == "AtlasTextGeometryProcessor") {
    includeModule(ShaderModuleID::AtlasTextGeometryFrag);
  }

  // Phase 2 (VS call): append the GP's VS function call so that any varying it writes
  // (transformedPosition / vLocal / etc.) is defined before phase 3 reads it.
  if (ShaderModuleRegistry::HasModule(geometryProcessor->name())) {
    includeVSModule(ShaderModuleRegistry::GetModuleID(geometryProcessor->name()));
    auto vsCallExpr = geometryProcessor->buildVSCallExpr(gpUniforms, gpVaryings);
    if (!vsCallExpr.empty()) {
      vertexShaderBuilder()->codeAppend(vsCallExpr);
    }
  }

  // Phase 3 (coord transform code): emit the `TransformedCoords_i = M * vec3(uv, 1)` statements,
  // consuming the uv expression (attribute or varying) now that it is defined by phase 2.
  auto coordInputExpr = geometryProcessor->coordTransformInputExpr(gpUniforms, gpVaryings);
  geometryProcessor->emitCoordTransformCode(args, vertexShaderBuilder(), coordInputExpr);

  auto colorResult = geometryProcessor->buildColorCallExpr(gpUniforms, gpVaryings);
  auto coverageResult = geometryProcessor->buildCoverageCallExpr(gpUniforms, gpVaryings);
  // GPs whose coverage formula uses helpers from tgfx_gp_coverage.glsl must pull the module in.
  // The set is closed and intentionally hard-coded: each entry corresponds to a TGFX_*Coverage
  // call emitted by the respective GP's buildCoverageCallExpr.
  const auto& gpName = geometryProcessor->name();
  if (gpName == "EllipseGeometryProcessor" || gpName == "RoundStrokeRectGeometryProcessor" ||
      gpName == "HairlineLineGeometryProcessor" || gpName == "HairlineQuadGeometryProcessor" ||
      gpName == "NonAARRectGeometryProcessor") {
    includeModule(ShaderModuleID::GPCoverage);
  }
  fragmentShaderBuilder()->codeAppend(RenderManifest(colorResult));
  fragmentShaderBuilder()->codeAppendf("%s = %s;\n", outputColor->c_str(),
                                       colorResult.outputVarName.c_str());
  fragmentShaderBuilder()->codeAppend(RenderManifest(coverageResult));
  fragmentShaderBuilder()->codeAppendf("%s = %s;\n", outputCoverage->c_str(),
                                       coverageResult.outputVarName.c_str());

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
  // Include XP module and blend mode utility functions.
  includeModule(ShaderModuleID::BlendModes);
  auto xpName = xferProcessor->name();
  if (ShaderModuleRegistry::HasModule(xpName)) {
    includeModule(ShaderModuleRegistry::GetModuleID(xpName));
  }
  fragmentShaderBuilder()->codeAppend(RenderManifest(result));

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
}

}  // namespace tgfx
