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
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

// ---- Helper: check if an FP can be handled by the modular path ----

// Returns true if the FP has a modular .glsl module.
static bool HasModularModule(const FragmentProcessor* fp) {
  return ShaderModuleRegistry::HasModule(fp->name());
}

// Returns true if the FP can be handled by the modular builder.
// Currently unused since CanUseModularPath() always returns true,
// but retained for potential future selective path switching.
__attribute__((unused)) static bool IsModularFP(const FragmentProcessor* fp) {
  auto name = fp->name();
  if (ShaderModuleRegistry::HasModule(name)) {
    return true;
  }
  // Container FPs and complex leaf FPs that fall back to legacy emitCode() within the modular
  // builder. They don't have .glsl modules but are safe to use in the modular path because
  // their emitCode() methods produce correct GLSL regardless of the builder type.
  if (name == "ClampedGradientEffect" || name == "ComposeFragmentProcessor" ||
      name == "XfermodeFragmentProcessor" || name == "GaussianBlur1DFragmentProcessor" ||
      name == "TextureEffect" || name == "TiledTextureEffect" ||
      name == "UnrolledBinaryGradientColorizer") {
    return true;
  }
  return false;
}

// ---- Public API ----

ModularProgramBuilder::ModularProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : GLSLProgramBuilder(context, programInfo) {
}

bool ModularProgramBuilder::CanUseModularPath(const ProgramInfo* programInfo) {
  // All fragment processors are now supported by the modular builder (either via modular .glsl
  // modules for leaf FPs, or via legacy emitCode() fallback for containers and complex FPs).
  // GP and XP use the same emitCode() paths as the legacy builder.
  (void)programInfo;
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
  auto& defs = fragmentShaderBuilder()->shaderStrings[ShaderBuilder::Type::Definitions];
  if (name == "ConstColorProcessor") {
    auto constColor = static_cast<const ConstColorProcessor*>(processor);
    defs += "#define TGFX_CONST_COLOR_INPUT_MODE ";
    defs += std::to_string(static_cast<int>(constColor->inputMode));
    defs += "\n";
  } else if (name == "DeviceSpaceTextureEffect") {
    auto deviceEffect = static_cast<const DeviceSpaceTextureEffect*>(processor);
    defs += "#define TGFX_DST_IS_ALPHA_ONLY ";
    defs += deviceEffect->textureProxy->isAlphaOnly() ? "1" : "0";
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
  currentTexSamplers.clear();
  FragmentProcessor::Iter fpIter(processor);
  int samplerIndex = 0;
  while (const auto subFP = fpIter.next()) {
    for (size_t i = 0; i < subFP->numTextureSamplers(); ++i) {
      std::string name = "TextureSampler_";
      name += std::to_string(samplerIndex++);
      auto texture = subFP->textureAt(i);
      currentTexSamplers.emplace_back(emitSampler(texture, name));
    }
  }

  auto name = processor->name();
  if (name == "ClampedGradientEffect") {
    emitClampedGradientEffect(processor, transformedCoordVarsIdx, input, output);
  } else if (HasModularModule(processor)) {
    emitLeafFPCall(processor, transformedCoordVarsIdx, input, output);
  } else {
    // Complex FP without a .glsl module — fall back to legacy emitCode() path.
    // Build the EmitArgs with the collected samplers and coord transforms.
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
  } else if (name == "RadialGradientLayout") {
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = FP_RadialGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "DiamondGradientLayout") {
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = FP_DiamondGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "ConicGradientLayout") {
    auto biasName =
        uniformHandler()->addUniform("Bias", UniformFormat::Float, ShaderStage::Fragment);
    auto scaleName =
        uniformHandler()->addUniform("Scale", UniformFormat::Float, ShaderStage::Fragment);
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = FP_ConicGradientLayout(%s, %s, %s);", output.c_str(),
                             coordName.c_str(), biasName.c_str(), scaleName.c_str());
  } else if (name == "AARectEffect") {
    auto rectName =
        uniformHandler()->addUniform("Rect", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_AARectEffect(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), rectName.c_str());
  } else if (name == "ColorMatrixFragmentProcessor") {
    auto matrixName =
        uniformHandler()->addUniform("Matrix", UniformFormat::Float4x4, ShaderStage::Fragment);
    auto vectorName =
        uniformHandler()->addUniform("Vector", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_ColorMatrix(%s, %s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), matrixName.c_str(),
                             vectorName.c_str());
  } else if (name == "LumaFragmentProcessor") {
    auto krName =
        uniformHandler()->addUniform("Kr", UniformFormat::Float, ShaderStage::Fragment);
    auto kgName =
        uniformHandler()->addUniform("Kg", UniformFormat::Float, ShaderStage::Fragment);
    auto kbName =
        uniformHandler()->addUniform("Kb", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_Luma(%s, %s, %s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), krName.c_str(),
                             kgName.c_str(), kbName.c_str());
  } else if (name == "DualIntervalGradientColorizer") {
    auto scale01Name = uniformHandler()->addUniform("scale01", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
    auto bias01Name = uniformHandler()->addUniform("bias01", UniformFormat::Float4,
                                                   ShaderStage::Fragment);
    auto scale23Name = uniformHandler()->addUniform("scale23", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
    auto bias23Name = uniformHandler()->addUniform("bias23", UniformFormat::Float4,
                                                   ShaderStage::Fragment);
    auto thresholdName = uniformHandler()->addUniform("threshold", UniformFormat::Float,
                                                      ShaderStage::Fragment);
    fragBuilder->codeAppendf(
        "%s = FP_DualIntervalGradientColorizer(%s, %s, %s, %s, %s, %s);", output.c_str(),
        input.empty() ? "vec4(1.0)" : input.c_str(), scale01Name.c_str(), bias01Name.c_str(),
        scale23Name.c_str(), bias23Name.c_str(), thresholdName.c_str());
  } else if (name == "AlphaThresholdFragmentProcessor") {
    auto thresholdName = uniformHandler()->addUniform("Threshold", UniformFormat::Float,
                                                      ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = FP_AlphaThreshold(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(),
                             thresholdName.c_str());
  } else if (name == "TextureGradientColorizer") {
    // TextureGradientColorizer uses one sampler; the sampler handle is at index 0.
    auto samplerVar = uniformHandler()->getSamplerVariable(currentTexSamplers[0]);
    fragBuilder->codeAppendf("vec2 coord = vec2(%s.x, 0.5);",
                             input.empty() ? "vec4(1.0)" : input.c_str());
    fragBuilder->codeAppendf("%s = ", output.c_str());
    fragBuilder->appendTextureLookup(currentTexSamplers[0], "coord");
    fragBuilder->codeAppend(";");
  } else if (name == "DeviceSpaceTextureEffect") {
    auto deviceEffect = static_cast<const DeviceSpaceTextureEffect*>(processor);
    auto deviceCoordMatrixName = uniformHandler()->addUniform(
        "DeviceCoordMatrix", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("vec3 deviceCoord = %s * vec3(gl_FragCoord.xy, 1.0);",
                             deviceCoordMatrixName.c_str());
    fragBuilder->codeAppendf("%s = ", output.c_str());
    fragBuilder->appendTextureLookup(currentTexSamplers[0], "deviceCoord.xy");
    fragBuilder->codeAppend(";");
    if (deviceEffect->textureProxy->isAlphaOnly()) {
      fragBuilder->codeAppendf("%s = %s.a * %s;", output.c_str(), output.c_str(),
                               input.empty() ? "vec4(1.0)" : input.c_str());
    } else {
      fragBuilder->codeAppendf("%s = %s * %s.a;", output.c_str(), output.c_str(),
                               output.c_str());
    }
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
  if (HasModularModule(gradLayout)) {
    emitLeafFPCall(gradLayout, gradLayoutCoordIdx, "vec4(1.0)", child1Var);
  } else {
    // Fallback to legacy emitCode() for unmodularized layout.
    FragmentProcessor::TransformedCoordVars coords(
        gradLayout, gradLayoutCoordIdx < transformedCoordVars.size()
                        ? &transformedCoordVars[gradLayoutCoordIdx]
                        : nullptr);
    FragmentProcessor::TextureSamplers textureSamplers(
        gradLayout, currentTexSamplers.empty() ? nullptr : &currentTexSamplers[0]);
    FragmentProcessor::EmitArgs childArgs(fragBuilder, uniformHandler(), child1Var, "vec4(1.0)",
                                          subsetVarName, &coords, &textureSamplers);
    gradLayout->emitCode(childArgs);
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
  if (HasModularModule(colorizer)) {
    // Declare the child output variable, then use emitLeafFPCall.
    fragBuilder->codeAppendf("vec4 %s;", child0Var.c_str());
    emitLeafFPCall(colorizer, transformedCoordVarsIdx, "t", child0Var);
  } else {
    // Complex colorizer without a modular module — fall back to legacy emitCode().
    fragBuilder->codeAppendf("vec4 %s;", child0Var.c_str());
    FragmentProcessor::TransformedCoordVars coords(
        colorizer, transformedCoordVarsIdx < transformedCoordVars.size()
                       ? &transformedCoordVars[transformedCoordVarsIdx]
                       : nullptr);
    FragmentProcessor::TextureSamplers textureSamplers(
        colorizer, currentTexSamplers.empty() ? nullptr : &currentTexSamplers[0]);
    FragmentProcessor::EmitArgs childArgs(fragBuilder, uniformHandler(), child0Var, "t",
                                          subsetVarName, &coords, &textureSamplers);
    colorizer->emitCode(childArgs);
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
