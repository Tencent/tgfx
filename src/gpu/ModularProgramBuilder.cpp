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

  // Collect texture samplers from this FP's entire subtree FIRST (before any dispatch).
  // All samplers are declared under the current processor's name-mangle scope,
  // matching the legacy emitAndInstallFragProc behavior.
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

  // Try polymorphic container dispatch.
  // The emitChild callback uses legacy EmitArgs + emitCode to process children,
  // using the samplers already collected above with childInputs() offset.
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
    // Use legacy-style child emission: push child scope, construct EmitArgs
    // with childInputs() offset, call emitCode.
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

  // Check if GP supports modular FS path (only migrated GPs return non-empty).
  auto gpName = geometryProcessor->name();
  bool useModularFS = (gpName == "DefaultGeometryProcessor");

  if (useModularFS) {
    // Modular FS path: call emitCode with skipFragmentCode=true for VS-only.
    MangledVaryings gpVaryings;
    MangledUniforms gpUniforms;
    GeometryProcessor::EmitArgs args(vertexShaderBuilder(), fragmentShaderBuilder(),
                                     varyingHandler(), uniformHandler(), getContext()->shaderCaps(),
                                     *outputColor, *outputCoverage, &transformHandler,
                                     &subsetVarName, true);
    args.gpVaryings = &gpVaryings;
    args.gpUniforms = &gpUniforms;
    geometryProcessor->emitCode(args);

    // Emit GP shader macros and generate FS function calls.
    ShaderMacroSet macros;
    geometryProcessor->onBuildShaderMacros(macros);
    if (!macros.empty()) {
      fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions] +=
          macros.toPreamble();
    }
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
