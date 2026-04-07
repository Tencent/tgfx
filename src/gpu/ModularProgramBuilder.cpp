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
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

// ---- Helper: check if an FP can be handled by the modular path ----

static bool IsModularFP(const FragmentProcessor* fp) {
  auto name = fp->name();
  if (ShaderModuleRegistry::HasModule(name)) {
    return true;
  }
  // ClampedGradientEffect is handled as inline control flow in main().
  if (name == "ClampedGradientEffect") {
    for (size_t i = 0; i < fp->numChildProcessors(); ++i) {
      if (!IsModularFP(fp->childProcessor(i))) {
        return false;
      }
    }
    return true;
  }
  return false;
}

// ---- Public API ----

ModularProgramBuilder::ModularProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : GLSLProgramBuilder(context, programInfo) {
}

bool ModularProgramBuilder::CanUseModularPath(const ProgramInfo* programInfo) {
  for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
    if (!IsModularFP(programInfo->getFragmentProcessor(i))) {
      return false;
    }
  }
  return true;
}

bool ModularProgramBuilder::emitAndInstallProcessors() {
  std::string inputColor;
  std::string inputCoverage;
  // Reuse GP emission from parent (unchanged).
  emitAndInstallGeoProc(&inputColor, &inputCoverage);
  // Use modular FP emission (new code).
  emitModularFragProcessors(&inputColor, &inputCoverage);
  // Reuse XP emission from parent (unchanged).
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
  if (id != ShaderModuleID::TypesGLSL &&
      includedModules.count(ShaderModuleID::TypesGLSL) == 0) {
    includeModule(ShaderModuleID::TypesGLSL);
  }
  fragmentShaderBuilder()->addFunction(ShaderModuleRegistry::GetModule(id));
}

// ---- Processor #define emission ----

void ModularProgramBuilder::emitProcessorDefines(const FragmentProcessor* processor) {
  auto name = processor->name();
  if (name == "ConstColorProcessor") {
    auto constColor = static_cast<const ConstColorProcessor*>(processor);
    auto& defs = fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions];
    defs += "#define TGFX_CONST_COLOR_INPUT_MODE ";
    defs += std::to_string(static_cast<int>(constColor->inputMode));
    defs += "\n";
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
    // Advance coord transform index using same pre-order traversal as legacy path.
    FragmentProcessor::Iter iter(fp);
    while (const FragmentProcessor* tempFP = iter.next()) {
      transformedCoordVarsIdx += tempFP->numCoordTransforms();
    }
    **inOut = output;
  }
}

std::string ModularProgramBuilder::emitModularFragProc(const FragmentProcessor* processor,
                                                       size_t transformedCoordVarsIdx,
                                                       const std::string& input) {
  // Use ProcessorGuard to push this processor for correct name mangling.
  currentProcessors.push_back(processor);
  std::string output;
  nameExpression(&output, "output");

  auto processorIndex = programInfo->getProcessorIndex(processor);
  fragmentShaderBuilder()->codeAppendf("{ // Processor%d : %s\n", processorIndex,
                                       processor->name().c_str());

  // Collect texture samplers from FP hierarchy (same as legacy path).
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

  auto name = processor->name();
  if (name == "ClampedGradientEffect") {
    emitClampedGradientEffect(processor, transformedCoordVarsIdx, input, output);
  } else if (ShaderModuleRegistry::HasModule(name)) {
    emitLeafFPCall(processor, transformedCoordVarsIdx, input, output);
  }

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
  return output;
}

// ---- Leaf FP function call emission ----

void ModularProgramBuilder::emitLeafFPCall(const FragmentProcessor* processor,
                                           size_t transformedCoordVarsIdx,
                                           const std::string& input, const std::string& output) {
  auto name = processor->name();
  auto moduleID = ShaderModuleRegistry::GetModuleID(name);
  emitProcessorDefines(processor);
  includeModule(moduleID);

  auto fragBuilder = fragmentShaderBuilder();

  if (name == "ConstColorProcessor") {
    // Declare uniform using legacy naming convention.
    auto colorName = uniformHandler()->addUniform("Color", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_ConstColor(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), colorName.c_str());
  } else if (name == "LinearGradientLayout") {
    // Get the transformed coordinate varying.
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = FP_LinearGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "SingleIntervalGradientColorizer") {
    auto startName = uniformHandler()->addUniform("start", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
    auto endName = uniformHandler()->addUniform("end", UniformFormat::Float4,
                                                ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_SingleIntervalGradientColorizer(%s, %s, %s);",
                             output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(),
                             startName.c_str(), endName.c_str());
  }
}

// ---- ClampedGradientEffect inline expansion ----

void ModularProgramBuilder::emitClampedGradientEffect(const FragmentProcessor* processor,
                                                      size_t transformedCoordVarsIdx,
                                                      const std::string& input,
                                                      const std::string& output) {
  auto clamped = static_cast<const ClampedGradientEffect*>(processor);
  auto fragBuilder = fragmentShaderBuilder();

  // Declare ClampedGradientEffect's own uniforms (leftBorderColor, rightBorderColor).
  auto leftBorderColorName = uniformHandler()->addUniform("leftBorderColor", UniformFormat::Float4,
                                                          ShaderStage::Fragment);
  auto rightBorderColorName = uniformHandler()->addUniform("rightBorderColor",
                                                           UniformFormat::Float4,
                                                           ShaderStage::Fragment);

  // --- Step 1: Emit gradLayout child ---
  // The gradLayout child is at childProcessors[gradLayoutIndex].
  const auto* gradLayout = processor->childProcessor(clamped->gradLayoutIndex);

  // Push gradLayout processor for correct mangle suffix.
  currentProcessors.push_back(gradLayout);

  // Compute the coord transform index offset for the gradLayout child.
  // In ClampedGradientEffect, child[0]=colorizer (no CoordTransform), child[1]=gradLayout.
  // We need to skip past the colorizer's subtree's coord transforms.
  size_t gradLayoutCoordIdx = transformedCoordVarsIdx;
  {
    // Count coord transforms from the ClampedGradientEffect's subtree up to the gradLayout.
    // The pre-order traversal visits: ClampedGradientEffect, colorizer, gradLayout.
    // ClampedGradientEffect itself has 0 coord transforms.
    // Colorizer subtree coord transforms must be skipped.
    const auto* colorizer = processor->childProcessor(clamped->colorizerIndex);
    FragmentProcessor::Iter colorizerIter(colorizer);
    while (const FragmentProcessor* tempFP = colorizerIter.next()) {
      gradLayoutCoordIdx += tempFP->numCoordTransforms();
    }
  }

  // Generate the child variable name using gradLayout's mangle suffix.
  std::string child1Var = "_child1";
  child1Var += programInfo->getMangledSuffix(processor);
  fragBuilder->codeAppendf("vec4 %s;", child1Var.c_str());

  // Include module and emit the gradLayout call.
  auto gradLayoutName = gradLayout->name();
  if (ShaderModuleRegistry::HasModule(gradLayoutName)) {
    auto moduleID = ShaderModuleRegistry::GetModuleID(gradLayoutName);
    emitProcessorDefines(gradLayout);
    includeModule(moduleID);

    if (gradLayoutName == "LinearGradientLayout") {
      auto& coordVar = transformedCoordVars[gradLayoutCoordIdx];
      auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
      fragBuilder->codeAppendf("%s = FP_LinearGradientLayout(%s);", child1Var.c_str(),
                               coordName.c_str());
    }
  }

  currentProcessors.pop_back();

  // --- Step 2: Clamping logic ---
  fragBuilder->codeAppendf("vec4 t = %s;", child1Var.c_str());
  fragBuilder->codeAppend("if (t.y < 0.0) {");
  fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
  fragBuilder->codeAppend("} else if (t.x <= 0.0) {");
  fragBuilder->codeAppendf("%s = %s;", output.c_str(), leftBorderColorName.c_str());
  fragBuilder->codeAppend("} else if (t.x >= 1.0) {");
  fragBuilder->codeAppendf("%s = %s;", output.c_str(), rightBorderColorName.c_str());
  fragBuilder->codeAppend("} else {");

  // --- Step 3: Emit colorizer child ---
  const auto* colorizer = processor->childProcessor(clamped->colorizerIndex);

  // Push colorizer processor for correct mangle suffix.
  currentProcessors.push_back(colorizer);

  std::string child0Var = "_child0";
  child0Var += programInfo->getMangledSuffix(processor);

  auto colorizerName = colorizer->name();
  if (ShaderModuleRegistry::HasModule(colorizerName)) {
    auto moduleID = ShaderModuleRegistry::GetModuleID(colorizerName);
    emitProcessorDefines(colorizer);
    includeModule(moduleID);

    if (colorizerName == "SingleIntervalGradientColorizer") {
      auto startName = uniformHandler()->addUniform("start", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
      auto endName = uniformHandler()->addUniform("end", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
      fragBuilder->codeAppendf("vec4 %s = FP_SingleIntervalGradientColorizer(t, %s, %s);",
                               child0Var.c_str(), startName.c_str(), endName.c_str());
    }
  }

  currentProcessors.pop_back();

  fragBuilder->codeAppendf("%s = %s;", output.c_str(), child0Var.c_str());
  fragBuilder->codeAppend("}");

  // --- Step 4: Premultiply and modulate by input alpha ---
  fragBuilder->codeAppendf("%s.rgb *= %s.a;", output.c_str(), output.c_str());
  fragBuilder->codeAppendf("%s *= %s.a;", output.c_str(),
                           input.empty() ? "vec4(1.0)" : input.c_str());
}

}  // namespace tgfx
