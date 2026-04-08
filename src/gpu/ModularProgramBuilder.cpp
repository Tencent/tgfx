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
#include "gpu/BlendFormula.h"
#include "gpu/MangledResources.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/resources/YUVTextureView.h"

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
      name == "XfermodeFragmentProcessor" || name == "GaussianBlur1DFragmentProcessor") {
    return true;
  }
  // Complex leaf FPs that are handled inline in emitLeafFPCall().
  if (name == "TextureEffect" || name == "TiledTextureEffect" ||
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

// Unused for now — reserved for future two-pass optimization.
void ModularProgramBuilder::registerFPResources(const FragmentProcessor* /*processor*/,
                                                size_t /*transformedCoordVarsIdx*/) {
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

  // Collect texture samplers from FP hierarchy (same as legacy path).
  // Skip for containers that recursively call emitModularFragProc() for children —
  // each child will collect its own samplers.
  // ClampedGradientEffect keeps collection because it manages children's samplers internally.
  auto name = processor->name();
  bool skipSamplerCollection =
      (name == "ComposeFragmentProcessor" || name == "XfermodeFragmentProcessor" ||
       name == "GaussianBlur1DFragmentProcessor");
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

  if (name == "ClampedGradientEffect") {
    emitClampedGradientEffect(processor, transformedCoordVarsIdx, input, output);
  } else if (name == "ComposeFragmentProcessor") {
    emitComposeFragmentProcessor(processor, transformedCoordVarsIdx, input, output);
  } else if (name == "XfermodeFragmentProcessor") {
    emitXfermodeFragmentProcessor(processor, transformedCoordVarsIdx, input, output);
  } else if (name == "GaussianBlur1DFragmentProcessor") {
    emitGaussianBlur1DFragmentProcessor(processor, transformedCoordVarsIdx, input, output);
  } else if (HasModularModule(processor) || name == "UnrolledBinaryGradientColorizer" ||
             name == "TextureEffect" || name == "TiledTextureEffect") {
    emitLeafFPCall(processor, transformedCoordVarsIdx, input, output, coordTransformFunc);
  } else {
    // Safety fallback for any unhandled FP type.
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
                                           size_t transformedCoordVarsIdx, const std::string& input,
                                           const std::string& output,
                                           const CoordTransformFunc& coordTransformFunc) {
  auto name = processor->name();

  // Polymorphic path: only for FPs with buildCallStatement AND .glsl module in registry.
  if (ShaderModuleRegistry::HasModule(name)) {
    FPResources resources;
    // Populate coord transform (emit perspective divide if needed).
    if (transformedCoordVarsIdx < transformedCoordVars.size()) {
      auto texCoordName = fragmentShaderBuilder()->emitPerspTextCoord(
          transformedCoordVars[transformedCoordVarsIdx]);
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
    if (!result.statement.empty()) {
      emitProcessorDefines(processor);
      includeModule(ShaderModuleRegistry::GetModuleID(name));
      fragmentShaderBuilder()->codeAppend(result.statement);
      if (result.outputVarName != output) {
        fragmentShaderBuilder()->codeAppendf("%s = %s;", output.c_str(),
                                             result.outputVarName.c_str());
      }
      return;
    }
    // buildCallStatement returned empty — fall through to name-based dispatch.
    // Note: declareResources may have registered uniforms; this is safe because
    // the fallback path will use the same uniform names.
    emitProcessorDefines(processor);
    includeModule(ShaderModuleRegistry::GetModuleID(name));
  }

  auto fragBuilder = fragmentShaderBuilder();

  if (name == "ConstColorProcessor") {
    // Declare uniform using legacy naming convention.
    auto colorName =
        uniformHandler()->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_ConstColor(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), colorName.c_str());
  } else if (name == "LinearGradientLayout") {
    // Get the transformed coordinate varying.
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = TGFX_LinearGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "SingleIntervalGradientColorizer") {
    auto startName =
        uniformHandler()->addUniform("start", UniformFormat::Float4, ShaderStage::Fragment);
    auto endName =
        uniformHandler()->addUniform("end", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_SingleIntervalGradientColorizer(%s, %s, %s);",
                             output.c_str(), input.empty() ? "vec4(1.0)" : input.c_str(),
                             startName.c_str(), endName.c_str());
  } else if (name == "RadialGradientLayout") {
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = TGFX_RadialGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "DiamondGradientLayout") {
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = TGFX_DiamondGradientLayout(%s);", output.c_str(),
                             coordName.c_str());
  } else if (name == "ConicGradientLayout") {
    auto biasName =
        uniformHandler()->addUniform("Bias", UniformFormat::Float, ShaderStage::Fragment);
    auto scaleName =
        uniformHandler()->addUniform("Scale", UniformFormat::Float, ShaderStage::Fragment);
    auto& coordVar = transformedCoordVars[transformedCoordVarsIdx];
    auto coordName = fragBuilder->emitPerspTextCoord(coordVar);
    fragBuilder->codeAppendf("%s = TGFX_ConicGradientLayout(%s, %s, %s);", output.c_str(),
                             coordName.c_str(), biasName.c_str(), scaleName.c_str());
  } else if (name == "AARectEffect") {
    auto rectName =
        uniformHandler()->addUniform("Rect", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_AARectEffect(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), rectName.c_str());
  } else if (name == "ColorMatrixFragmentProcessor") {
    auto matrixName =
        uniformHandler()->addUniform("Matrix", UniformFormat::Float4x4, ShaderStage::Fragment);
    auto vectorName =
        uniformHandler()->addUniform("Vector", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_ColorMatrix(%s, %s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), matrixName.c_str(),
                             vectorName.c_str());
  } else if (name == "LumaFragmentProcessor") {
    auto krName = uniformHandler()->addUniform("Kr", UniformFormat::Float, ShaderStage::Fragment);
    auto kgName = uniformHandler()->addUniform("Kg", UniformFormat::Float, ShaderStage::Fragment);
    auto kbName = uniformHandler()->addUniform("Kb", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_Luma(%s, %s, %s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), krName.c_str(),
                             kgName.c_str(), kbName.c_str());
  } else if (name == "DualIntervalGradientColorizer") {
    auto scale01Name =
        uniformHandler()->addUniform("scale01", UniformFormat::Float4, ShaderStage::Fragment);
    auto bias01Name =
        uniformHandler()->addUniform("bias01", UniformFormat::Float4, ShaderStage::Fragment);
    auto scale23Name =
        uniformHandler()->addUniform("scale23", UniformFormat::Float4, ShaderStage::Fragment);
    auto bias23Name =
        uniformHandler()->addUniform("bias23", UniformFormat::Float4, ShaderStage::Fragment);
    auto thresholdName =
        uniformHandler()->addUniform("threshold", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_DualIntervalGradientColorizer(%s, %s, %s, %s, %s, %s);",
                             output.c_str(), input.empty() ? "vec4(1.0)" : input.c_str(),
                             scale01Name.c_str(), bias01Name.c_str(), scale23Name.c_str(),
                             bias23Name.c_str(), thresholdName.c_str());
  } else if (name == "AlphaThresholdFragmentProcessor") {
    auto thresholdName =
        uniformHandler()->addUniform("Threshold", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = TGFX_AlphaThreshold(%s, %s);", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str(), thresholdName.c_str());
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
      fragBuilder->codeAppendf("%s = %s * %s.a;", output.c_str(), output.c_str(), output.c_str());
    }
  } else if (name == "UnrolledBinaryGradientColorizer") {
    emitUnrolledBinaryGradientColorizer(processor, input, output);
  } else if (name == "TextureEffect") {
    emitTextureEffect(processor, transformedCoordVarsIdx, input, output, coordTransformFunc);
  } else if (name == "TiledTextureEffect") {
    emitTiledTextureEffect(processor, transformedCoordVarsIdx, input, output, coordTransformFunc);
  }
}

// ---- UnrolledBinaryGradientColorizer inline emission ----

static std::string AddUniformIfNeeded(UniformHandler* handler, const std::string& name,
                                      int intervalCount, int limit) {
  if (intervalCount > limit) {
    return handler->addUniform(name, UniformFormat::Float4, ShaderStage::Fragment);
  }
  return "";
}

static void AppendBinaryTreeCode1(FragmentShaderBuilder* fragBuilder, int intervalCount,
                                  const std::string& scale0_1, const std::string& scale2_3,
                                  const std::string& scale4_5, const std::string& scale6_7,
                                  const std::string& bias0_1, const std::string& bias2_3,
                                  const std::string& bias4_5, const std::string& bias6_7,
                                  const std::string& thresholds1_7) {
  if (intervalCount >= 2) {
    fragBuilder->codeAppendf("if (t < %s.y) {", thresholds1_7.c_str());
  }
  fragBuilder->codeAppendf("if (t < %s.x) {", thresholds1_7.c_str());
  fragBuilder->codeAppendf("scale = %s;", scale0_1.c_str());
  fragBuilder->codeAppendf("bias = %s;", bias0_1.c_str());
  if (intervalCount > 1) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;", scale2_3.empty() ? "vec4(0.0)" : scale2_3.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias2_3.empty() ? "vec4(0.0)" : bias2_3.c_str());
  }
  fragBuilder->codeAppend("}");
  if (intervalCount > 2) {
    fragBuilder->codeAppend("} else {");
  }
  if (intervalCount >= 3) {
    fragBuilder->codeAppendf("if (t < %s.z) {", thresholds1_7.c_str());
    fragBuilder->codeAppendf("scale = %s;", scale4_5.empty() ? "vec4(0.0)" : scale4_5.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias4_5.empty() ? "vec4(0.0)" : bias4_5.c_str());
  }
  if (intervalCount > 3) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;", scale6_7.empty() ? "vec4(0.0)" : scale6_7.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias6_7.empty() ? "vec4(0.0)" : bias6_7.c_str());
  }
  if (intervalCount >= 3) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount >= 2) {
    fragBuilder->codeAppend("}");
  }
}

static void AppendBinaryTreeCode2(FragmentShaderBuilder* fragBuilder, int intervalCount,
                                  const std::string& scale8_9, const std::string& scale10_11,
                                  const std::string& scale12_13, const std::string& scale14_15,
                                  const std::string& bias8_9, const std::string& bias10_11,
                                  const std::string& bias12_13, const std::string& bias14_15,
                                  const std::string& thresholds9_13) {
  if (intervalCount >= 6) {
    fragBuilder->codeAppendf("if (t < %s.y) {", thresholds9_13.c_str());
  }
  if (intervalCount >= 5) {
    fragBuilder->codeAppendf("if (t < %s.x) {", thresholds9_13.c_str());
    fragBuilder->codeAppendf("scale = %s;", scale8_9.empty() ? "vec4(0.0)" : scale8_9.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias8_9.empty() ? "vec4(0.0)" : bias8_9.c_str());
  }
  if (intervalCount > 5) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;", scale10_11.empty() ? "vec4(0.0)" : scale10_11.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias10_11.empty() ? "vec4(0.0)" : bias10_11.c_str());
  }
  if (intervalCount >= 5) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount > 6) {
    fragBuilder->codeAppend("} else {");
  }
  if (intervalCount >= 7) {
    fragBuilder->codeAppendf("if (t < %s.z) {", thresholds9_13.c_str());
    fragBuilder->codeAppendf("scale = %s;", scale12_13.empty() ? "vec4(0.0)" : scale12_13.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias12_13.empty() ? "vec4(0.0)" : bias12_13.c_str());
  }
  if (intervalCount > 7) {
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("scale = %s;", scale14_15.empty() ? "vec4(0.0)" : scale14_15.c_str());
    fragBuilder->codeAppendf("bias = %s;", bias14_15.empty() ? "vec4(0.0)" : bias14_15.c_str());
  }
  if (intervalCount >= 7) {
    fragBuilder->codeAppend("}");
  }
  if (intervalCount >= 6) {
    fragBuilder->codeAppend("}");
  }
}

void ModularProgramBuilder::emitUnrolledBinaryGradientColorizer(const FragmentProcessor* processor,
                                                                const std::string& input,
                                                                const std::string& output) {
  auto colorizer = static_cast<const UnrolledBinaryGradientColorizer*>(processor);
  auto fragBuilder = fragmentShaderBuilder();
  auto uHandler = uniformHandler();
  int ic = colorizer->intervalCount;

  auto scale0_1 = AddUniformIfNeeded(uHandler, "scale0_1", ic, 0);
  auto scale2_3 = AddUniformIfNeeded(uHandler, "scale2_3", ic, 1);
  auto scale4_5 = AddUniformIfNeeded(uHandler, "scale4_5", ic, 2);
  auto scale6_7 = AddUniformIfNeeded(uHandler, "scale6_7", ic, 3);
  auto scale8_9 = AddUniformIfNeeded(uHandler, "scale8_9", ic, 4);
  auto scale10_11 = AddUniformIfNeeded(uHandler, "scale10_11", ic, 5);
  auto scale12_13 = AddUniformIfNeeded(uHandler, "scale12_13", ic, 6);
  auto scale14_15 = AddUniformIfNeeded(uHandler, "scale14_15", ic, 7);
  auto bias0_1 = AddUniformIfNeeded(uHandler, "bias0_1", ic, 0);
  auto bias2_3 = AddUniformIfNeeded(uHandler, "bias2_3", ic, 1);
  auto bias4_5 = AddUniformIfNeeded(uHandler, "bias4_5", ic, 2);
  auto bias6_7 = AddUniformIfNeeded(uHandler, "bias6_7", ic, 3);
  auto bias8_9 = AddUniformIfNeeded(uHandler, "bias8_9", ic, 4);
  auto bias10_11 = AddUniformIfNeeded(uHandler, "bias10_11", ic, 5);
  auto bias12_13 = AddUniformIfNeeded(uHandler, "bias12_13", ic, 6);
  auto bias14_15 = AddUniformIfNeeded(uHandler, "bias14_15", ic, 7);
  auto thresholds1_7 =
      uHandler->addUniform("thresholds1_7", UniformFormat::Float4, ShaderStage::Fragment);
  auto thresholds9_13 =
      uHandler->addUniform("thresholds9_13", UniformFormat::Float4, ShaderStage::Fragment);

  fragBuilder->codeAppendf("float t = %s.x;", input.empty() ? "vec4(1.0)" : input.c_str());
  fragBuilder->codeAppend("vec4 scale, bias;");

  if (ic >= 4) {
    fragBuilder->codeAppendf("if (t < %s.w) {", thresholds1_7.c_str());
  }
  AppendBinaryTreeCode1(fragBuilder, ic, scale0_1, scale2_3, scale4_5, scale6_7, bias0_1, bias2_3,
                        bias4_5, bias6_7, thresholds1_7);
  if (ic > 4) {
    fragBuilder->codeAppend("} else {");
  }
  AppendBinaryTreeCode2(fragBuilder, ic, scale8_9, scale10_11, scale12_13, scale14_15, bias8_9,
                        bias10_11, bias12_13, bias14_15, thresholds9_13);
  if (ic >= 4) {
    fragBuilder->codeAppend("}");
  }

  fragBuilder->codeAppendf("%s = vec4(t * scale + bias);", output.c_str());
}

// ---- TextureEffect inline emission ----

void ModularProgramBuilder::emitTextureEffect(const FragmentProcessor* processor,
                                              size_t transformedCoordVarsIdx,
                                              const std::string& input, const std::string& output,
                                              const CoordTransformFunc& coordTransformFunc) {
  auto texEffect = static_cast<const TextureEffect*>(processor);
  auto fragBuilder = fragmentShaderBuilder();
  auto uHandler = uniformHandler();
  auto textureView = texEffect->getTextureView();
  if (textureView == nullptr) {
    fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
    return;
  }
  auto texCoordName =
      fragBuilder->emitPerspTextCoord(transformedCoordVars[transformedCoordVarsIdx]);
  if (coordTransformFunc) {
    fragBuilder->codeAppendf("highp vec2 transformedCoord = %s;",
                             coordTransformFunc(texCoordName).c_str());
    texCoordName = "transformedCoord";
  }

  std::string subsetName;
  if (texEffect->needSubset()) {
    subsetName = uHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
  }
  std::string extraSubsetName;
  if (texEffect->constraint == SrcRectConstraint::Strict) {
    extraSubsetName = subsetVarName;
  }

  // appendClamp helper inline
  auto appendClamp = [&](const std::string& vertexColor, const std::string& finalCoordName) {
    fragBuilder->codeAppendf("%s = %s;", finalCoordName.c_str(), vertexColor.c_str());
    if (!extraSubsetName.empty()) {
      fragBuilder->codeAppend("{");
      fragBuilder->codeAppendf("%s = clamp(%s, %s.xy, %s.zw);", finalCoordName.c_str(),
                               vertexColor.c_str(), extraSubsetName.c_str(),
                               extraSubsetName.c_str());
      fragBuilder->codeAppend("}");
    }
    if (!subsetName.empty()) {
      fragBuilder->codeAppendf("%s = clamp(%s, %s.xy, %s.zw);", finalCoordName.c_str(),
                               finalCoordName.c_str(), subsetName.c_str(), subsetName.c_str());
    }
  };

  if (textureView->isYUV()) {
    // ---- YUV path ----
    auto yuvTexture = texEffect->getYUVTexture();
    fragBuilder->codeAppend("highp vec2 finalCoord;");
    appendClamp(texCoordName, "finalCoord");
    fragBuilder->codeAppend("vec3 yuv;");
    fragBuilder->codeAppend("yuv.x = ");
    fragBuilder->appendTextureLookup(currentTexSamplers[0], "finalCoord");
    fragBuilder->codeAppend(".r;");
    if (yuvTexture->yuvFormat() == YUVFormat::I420) {
      appendClamp(texCoordName, "finalCoord");
      fragBuilder->codeAppend("yuv.y = ");
      fragBuilder->appendTextureLookup(currentTexSamplers[1], "finalCoord");
      fragBuilder->codeAppend(".r;");
      appendClamp(texCoordName, "finalCoord");
      fragBuilder->codeAppend("yuv.z = ");
      fragBuilder->appendTextureLookup(currentTexSamplers[2], "finalCoord");
      fragBuilder->codeAppend(".r;");
    } else if (yuvTexture->yuvFormat() == YUVFormat::NV12) {
      appendClamp(texCoordName, "finalCoord");
      fragBuilder->codeAppend("yuv.yz = ");
      fragBuilder->appendTextureLookup(currentTexSamplers[1], "finalCoord");
      fragBuilder->codeAppend(".ra;");
    }
    if (IsLimitedYUVColorRange(yuvTexture->yuvColorSpace())) {
      fragBuilder->codeAppend("yuv.x -= (16.0 / 255.0);");
    }
    fragBuilder->codeAppend("yuv.yz -= vec2(0.5, 0.5);");
    auto mat3Name =
        uHandler->addUniform("Mat3ColorConversion", UniformFormat::Float3x3, ShaderStage::Fragment);
    fragBuilder->codeAppendf("vec3 rgb = clamp(%s * yuv, 0.0, 1.0);", mat3Name.c_str());
    if (texEffect->alphaStart == Point::Zero()) {
      fragBuilder->codeAppendf("%s = vec4(rgb, 1.0);", output.c_str());
    } else {
      auto alphaStartName =
          uHandler->addUniform("AlphaStart", UniformFormat::Float2, ShaderStage::Fragment);
      fragBuilder->codeAppendf("vec2 alphaVertexColor = finalCoord + %s;", alphaStartName.c_str());
      fragBuilder->codeAppend("float yuv_a = ");
      fragBuilder->appendTextureLookup(currentTexSamplers[0], "alphaVertexColor");
      fragBuilder->codeAppend(".r;");
      fragBuilder->codeAppend("yuv_a = (yuv_a - 16.0/255.0) / (219.0/255.0 - 1.0/255.0);");
      fragBuilder->codeAppend("yuv_a = clamp(yuv_a, 0.0, 1.0);");
      fragBuilder->codeAppendf("%s = vec4(rgb * yuv_a, yuv_a);", output.c_str());
    }
  } else {
    // ---- RGB path ----
    fragBuilder->codeAppend("highp vec2 finalCoord;");
    appendClamp(texCoordName, "finalCoord");
    fragBuilder->codeAppend("vec4 color = ");
    fragBuilder->appendTextureLookup(currentTexSamplers[0], "finalCoord");
    fragBuilder->codeAppend(";");
    if (texEffect->alphaStart != Point::Zero()) {
      fragBuilder->codeAppend("color = clamp(color, 0.0, 1.0);");
      auto alphaStartName =
          uHandler->addUniform("AlphaStart", UniformFormat::Float2, ShaderStage::Fragment);
      fragBuilder->codeAppendf("vec2 alphaVertexColor = finalCoord + %s;", alphaStartName.c_str());
      fragBuilder->codeAppend("vec4 alpha = ");
      fragBuilder->appendTextureLookup(currentTexSamplers[0], "alphaVertexColor");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend("alpha = clamp(alpha, 0.0, 1.0);");
      fragBuilder->codeAppend("color = vec4(color.rgb * alpha.r, alpha.r);");
    }
    fragBuilder->codeAppendf("%s = color;", output.c_str());
  }

  // ---- Alpha-only / premultiply tail ----
  if (texEffect->textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", output.c_str(), output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", output.c_str(), output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str());
  }
}

// ---- TiledTextureEffect inline emission ----

// ShaderMode integer constants (mirrors TiledTextureEffect::ShaderMode which is protected).
static constexpr int kShaderMode_None = 0;
static constexpr int kShaderMode_Clamp = 1;
static constexpr int kShaderMode_RepeatNearestNone = 2;
static constexpr int kShaderMode_RepeatLinearNone = 3;
static constexpr int kShaderMode_RepeatLinearMipmap = 4;
static constexpr int kShaderMode_RepeatNearestMipmap = 5;
static constexpr int kShaderMode_MirrorRepeat = 6;
static constexpr int kShaderMode_ClampToBorderNearest = 7;
static constexpr int kShaderMode_ClampToBorderLinear = 8;

static bool TiledShaderModeRequiresUnormCoord(int mode) {
  switch (mode) {
    case kShaderMode_None:
    case kShaderMode_Clamp:
    case kShaderMode_RepeatNearestNone:
    case kShaderMode_MirrorRepeat:
      return false;
    default:
      return true;
  }
}

static bool TiledShaderModeUsesSubset(int m) {
  switch (m) {
    case kShaderMode_None:
    case kShaderMode_Clamp:
    case kShaderMode_ClampToBorderLinear:
      return false;
    default:
      return true;
  }
}

static bool TiledShaderModeUsesClamp(int m) {
  switch (m) {
    case kShaderMode_None:
    case kShaderMode_ClampToBorderNearest:
      return false;
    default:
      return true;
  }
}

static void EmitTiledSubsetCoord(FragmentShaderBuilder* fragBuilder, int mode,
                                 const std::string& subsetName, const char* coordSwizzle,
                                 const char* subsetStartSwizzle, const char* subsetStopSwizzle,
                                 const char* extraCoord, const char* coordWeight) {
  switch (mode) {
    case kShaderMode_None:
    case kShaderMode_ClampToBorderNearest:
    case kShaderMode_ClampToBorderLinear:
    case kShaderMode_Clamp:
      fragBuilder->codeAppendf("subsetCoord.%s = inCoord.%s;", coordSwizzle, coordSwizzle);
      break;
    case kShaderMode_RepeatNearestNone:
    case kShaderMode_RepeatLinearNone:
      fragBuilder->codeAppendf("subsetCoord.%s = mod(inCoord.%s - %s.%s, %s.%s - %s.%s) + %s.%s;",
                               coordSwizzle, coordSwizzle, subsetName.c_str(), subsetStartSwizzle,
                               subsetName.c_str(), subsetStopSwizzle, subsetName.c_str(),
                               subsetStartSwizzle, subsetName.c_str(), subsetStartSwizzle);
      break;
    case kShaderMode_RepeatNearestMipmap:
    case kShaderMode_RepeatLinearMipmap:
      fragBuilder->codeAppend("{");
      fragBuilder->codeAppendf("float w = %s.%s - %s.%s;", subsetName.c_str(), subsetStopSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppend("float w2 = 2.0 * w;");
      fragBuilder->codeAppendf("float d = inCoord.%s - %s.%s;", coordSwizzle, subsetName.c_str(),
                               subsetStartSwizzle);
      fragBuilder->codeAppend("float m = mod(d, w2);");
      fragBuilder->codeAppend("float o = mix(m, w2 - m, step(w, m));");
      fragBuilder->codeAppendf("subsetCoord.%s = o + %s.%s;", coordSwizzle, subsetName.c_str(),
                               subsetStartSwizzle);
      fragBuilder->codeAppendf("%s = w - o + %s.%s;", extraCoord, subsetName.c_str(),
                               subsetStartSwizzle);
      fragBuilder->codeAppend("float hw = w / 2.0;");
      fragBuilder->codeAppend("float n = mod(d - hw, w2);");
      fragBuilder->codeAppendf("%s = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);",
                               coordWeight);
      fragBuilder->codeAppend("}");
      break;
    case kShaderMode_MirrorRepeat:
      fragBuilder->codeAppend("{");
      fragBuilder->codeAppendf("float w = %s.%s - %s.%s;", subsetName.c_str(), subsetStopSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppend("float w2 = 2.0 * w;");
      fragBuilder->codeAppendf("float m = mod(inCoord.%s - %s.%s, w2);", coordSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppendf("subsetCoord.%s = mix(m, w2 - m, step(w, m)) + %s.%s;", coordSwizzle,
                               subsetName.c_str(), subsetStartSwizzle);
      fragBuilder->codeAppend("}");
      break;
  }
}

static void EmitTiledClampCoord(FragmentShaderBuilder* fragBuilder, bool clamp,
                                const std::string& clampName, const char* coordSwizzle,
                                const char* clampStartSwizzle, const char* clampStopSwizzle) {
  if (clamp) {
    fragBuilder->codeAppendf("clampedCoord%s = clamp(subsetCoord%s, %s%s, %s%s);", coordSwizzle,
                             coordSwizzle, clampName.c_str(), clampStartSwizzle, clampName.c_str(),
                             clampStopSwizzle);
  } else {
    fragBuilder->codeAppendf("clampedCoord%s = subsetCoord%s;", coordSwizzle, coordSwizzle);
  }
}

static void EmitTiledReadColor(FragmentShaderBuilder* fragBuilder, SamplerHandle samplerHandle,
                               const std::string& dimensionsName, const std::string& coord,
                               const char* out) {
  std::string normCoord;
  if (!dimensionsName.empty()) {
    normCoord = "(" + coord + ") * " + dimensionsName;
  } else {
    normCoord = coord;
  }
  fragBuilder->codeAppendf("vec4 %s = ", out);
  fragBuilder->appendTextureLookup(samplerHandle, normCoord);
  fragBuilder->codeAppend(";");
}

void ModularProgramBuilder::emitTiledTextureEffect(const FragmentProcessor* processor,
                                                   size_t transformedCoordVarsIdx,
                                                   const std::string& input,
                                                   const std::string& output,
                                                   const CoordTransformFunc& coordTransformFunc) {
  auto tiledEffect = static_cast<const TiledTextureEffect*>(processor);
  auto fragBuilder = fragmentShaderBuilder();
  auto uHandler = uniformHandler();
  auto textureView = tiledEffect->getTextureView();
  if (textureView == nullptr) {
    fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
    return;
  }
  auto texCoordName =
      fragBuilder->emitPerspTextCoord(transformedCoordVars[transformedCoordVarsIdx]);
  if (coordTransformFunc) {
    fragBuilder->codeAppendf("highp vec2 transformedCoord = %s;",
                             coordTransformFunc(texCoordName).c_str());
    texCoordName = "transformedCoord";
  }

  TiledTextureEffect::Sampling sampling(textureView, tiledEffect->samplerState,
                                        tiledEffect->subset);

  auto modeX = static_cast<int>(sampling.shaderModeX);
  auto modeY = static_cast<int>(sampling.shaderModeY);

  if (modeX == kShaderMode_None && modeY == kShaderMode_None) {
    fragBuilder->codeAppendf("%s = ", output.c_str());
    fragBuilder->appendTextureLookup(currentTexSamplers[0], texCoordName);
    fragBuilder->codeAppend(";");
  } else {
    fragBuilder->codeAppendf("vec2 inCoord = %s;", texCoordName.c_str());
    bool useClamp[2] = {TiledShaderModeUsesClamp(modeX), TiledShaderModeUsesClamp(modeY)};

    // Init uniforms.
    std::string subsetName, clampName, dimensionsName;
    if (TiledShaderModeUsesSubset(modeX) || TiledShaderModeUsesSubset(modeY)) {
      subsetName = uHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
    }
    if (useClamp[0] || useClamp[1]) {
      clampName = uHandler->addUniform("Clamp", UniformFormat::Float4, ShaderStage::Fragment);
    }
    bool unormRequired =
        TiledShaderModeRequiresUnormCoord(modeX) || TiledShaderModeRequiresUnormCoord(modeY);
    bool mustNormalize = textureView->getTexture()->type() != TextureType::Rectangle;
    if (unormRequired && mustNormalize) {
      dimensionsName =
          uHandler->addUniform("Dimension", UniformFormat::Float2, ShaderStage::Fragment);
    }
    if (!dimensionsName.empty()) {
      fragBuilder->codeAppendf("inCoord /= %s;", dimensionsName.c_str());
    }

    bool mipmapRepeatX =
        modeX == kShaderMode_RepeatNearestMipmap || modeX == kShaderMode_RepeatLinearMipmap;
    bool mipmapRepeatY =
        modeY == kShaderMode_RepeatNearestMipmap || modeY == kShaderMode_RepeatLinearMipmap;

    const char* extraRepeatCoordX = nullptr;
    const char* repeatCoordWeightX = nullptr;
    const char* extraRepeatCoordY = nullptr;
    const char* repeatCoordWeightY = nullptr;
    if (mipmapRepeatX || mipmapRepeatY) {
      fragBuilder->codeAppend("vec2 extraRepeatCoord;");
    }
    if (mipmapRepeatX) {
      fragBuilder->codeAppend("float repeatCoordWeightX;");
      extraRepeatCoordX = "extraRepeatCoord.x";
      repeatCoordWeightX = "repeatCoordWeightX";
    }
    if (mipmapRepeatY) {
      fragBuilder->codeAppend("float repeatCoordWeightY;");
      extraRepeatCoordY = "extraRepeatCoord.y";
      repeatCoordWeightY = "repeatCoordWeightY";
    }

    fragBuilder->codeAppend("highp vec2 subsetCoord;");
    EmitTiledSubsetCoord(fragBuilder, modeX, subsetName, "x", "x", "z", extraRepeatCoordX,
                         repeatCoordWeightX);
    EmitTiledSubsetCoord(fragBuilder, modeY, subsetName, "y", "y", "w", extraRepeatCoordY,
                         repeatCoordWeightY);

    fragBuilder->codeAppend("highp vec2 clampedCoord;");
    if (useClamp[0] == useClamp[1]) {
      EmitTiledClampCoord(fragBuilder, useClamp[0], clampName, "", ".xy", ".zw");
    } else {
      EmitTiledClampCoord(fragBuilder, useClamp[0], clampName, ".x", ".x", ".z");
      EmitTiledClampCoord(fragBuilder, useClamp[1], clampName, ".y", ".y", ".w");
    }

    if (tiledEffect->constraint == SrcRectConstraint::Strict) {
      std::string sName = subsetVarName;
      if (!dimensionsName.empty()) {
        fragBuilder->codeAppendf("highp vec4 extraSubset = %s;", sName.c_str());
        sName = "extraSubset";
        fragBuilder->codeAppendf("extraSubset.xy /= %s;", dimensionsName.c_str());
        fragBuilder->codeAppendf("extraSubset.zw /= %s;", dimensionsName.c_str());
      }
      fragBuilder->codeAppendf("clampedCoord = clamp(clampedCoord, %s.xy, %s.zw);", sName.c_str(),
                               sName.c_str());
    }

    if (mipmapRepeatX && mipmapRepeatY) {
      fragBuilder->codeAppendf("extraRepeatCoord = clamp(extraRepeatCoord, %s.xy, %s.zw);",
                               clampName.c_str(), clampName.c_str());
    } else if (mipmapRepeatX) {
      fragBuilder->codeAppendf("extraRepeatCoord.x = clamp(extraRepeatCoord.x, %s.x, %s.z);",
                               clampName.c_str(), clampName.c_str());
    } else if (mipmapRepeatY) {
      fragBuilder->codeAppendf("extraRepeatCoord.y = clamp(extraRepeatCoord.y, %s.y, %s.w);",
                               clampName.c_str(), clampName.c_str());
    }

    auto sampler = currentTexSamplers[0];
    if (mipmapRepeatX && mipmapRepeatY) {
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "clampedCoord", "textureColor1");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName,
                         "vec2(extraRepeatCoord.x, clampedCoord.y)", "textureColor2");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName,
                         "vec2(clampedCoord.x, extraRepeatCoord.y)", "textureColor3");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName,
                         "vec2(extraRepeatCoord.x, extraRepeatCoord.y)", "textureColor4");
      fragBuilder->codeAppend(
          "vec4 textureColor = mix(mix(textureColor1, textureColor2, repeatCoordWeightX), "
          "mix(textureColor3, textureColor4, repeatCoordWeightX), repeatCoordWeightY);");
    } else if (mipmapRepeatX) {
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "clampedCoord", "textureColor1");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName,
                         "vec2(extraRepeatCoord.x, clampedCoord.y)", "textureColor2");
      fragBuilder->codeAppend(
          "vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightX);");
    } else if (mipmapRepeatY) {
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "clampedCoord", "textureColor1");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName,
                         "vec2(clampedCoord.x, extraRepeatCoord.y)", "textureColor2");
      fragBuilder->codeAppend(
          "vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightY);");
    } else {
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "clampedCoord", "textureColor");
    }

    bool repeatX = modeX == kShaderMode_RepeatLinearNone || modeX == kShaderMode_RepeatLinearMipmap;
    bool repeatY = modeY == kShaderMode_RepeatLinearNone || modeY == kShaderMode_RepeatLinearMipmap;
    if (repeatX || modeX == kShaderMode_ClampToBorderLinear) {
      fragBuilder->codeAppend("float errX = subsetCoord.x - clampedCoord.x;");
      if (repeatX) {
        fragBuilder->codeAppendf("float repeatCoordX = errX > 0.0 ? %s.x : %s.z;",
                                 clampName.c_str(), clampName.c_str());
      }
    }
    if (repeatY || modeY == kShaderMode_ClampToBorderLinear) {
      fragBuilder->codeAppend("float errY = subsetCoord.y - clampedCoord.y;");
      if (repeatY) {
        fragBuilder->codeAppendf("float repeatCoordY = errY > 0.0 ? %s.y : %s.w;",
                                 clampName.c_str(), clampName.c_str());
      }
    }

    const char* ifStr = "if";
    if (repeatX && repeatY) {
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "vec2(repeatCoordX, clampedCoord.y)",
                         "repeatReadX");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "vec2(clampedCoord.x, repeatCoordY)",
                         "repeatReadY");
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "vec2(repeatCoordX, repeatCoordY)",
                         "repeatReadXY");
      fragBuilder->codeAppend("if (errX != 0.0 && errY != 0.0) {");
      fragBuilder->codeAppend("errX = abs(errX);");
      fragBuilder->codeAppend(
          "textureColor = mix(mix(textureColor, repeatReadX, errX), "
          "mix(repeatReadY, repeatReadXY, errX), abs(errY));");
      fragBuilder->codeAppend("}");
      ifStr = "else if";
    }
    if (repeatX) {
      fragBuilder->codeAppendf("%s (errX != 0.0) {", ifStr);
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "vec2(repeatCoordX, clampedCoord.y)",
                         "repeatReadX");
      fragBuilder->codeAppend("textureColor = mix(textureColor, repeatReadX, errX);");
      fragBuilder->codeAppend("}");
    }
    if (repeatY) {
      fragBuilder->codeAppendf("%s (errY != 0.0) {", ifStr);
      EmitTiledReadColor(fragBuilder, sampler, dimensionsName, "vec2(clampedCoord.x, repeatCoordY)",
                         "repeatReadY");
      fragBuilder->codeAppend("textureColor = mix(textureColor, repeatReadY, errY);");
      fragBuilder->codeAppend("}");
    }

    if (modeX == kShaderMode_ClampToBorderLinear) {
      fragBuilder->codeAppend("textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));");
    }
    if (modeY == kShaderMode_ClampToBorderLinear) {
      fragBuilder->codeAppend("textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));");
    }
    if (modeX == kShaderMode_ClampToBorderNearest) {
      fragBuilder->codeAppend("float snappedX = floor(inCoord.x + 0.001) + 0.5;");
      fragBuilder->codeAppendf("if (snappedX < %s.x || snappedX > %s.z) {", subsetName.c_str(),
                               subsetName.c_str());
      fragBuilder->codeAppend("textureColor = vec4(0.0);");
      fragBuilder->codeAppend("}");
    }
    if (modeY == kShaderMode_ClampToBorderNearest) {
      fragBuilder->codeAppend("float snappedY = floor(inCoord.y + 0.001) + 0.5;");
      fragBuilder->codeAppendf("if (snappedY < %s.y || snappedY > %s.w) {", subsetName.c_str(),
                               subsetName.c_str());
      fragBuilder->codeAppend("textureColor = vec4(0.0);");
      fragBuilder->codeAppend("}");
    }
    fragBuilder->codeAppendf("%s = textureColor;", output.c_str());
  }
  if (tiledEffect->textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", output.c_str(), output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", output.c_str(), output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str());
  }
}

// ---- Helper: compute child coordVarsIdx offset ----

size_t ModularProgramBuilder::childCoordVarsOffset(const FragmentProcessor* parent,
                                                   size_t parentCoordVarsIdx,
                                                   size_t childIndex) const {
  size_t offset = parentCoordVarsIdx;
  // Skip parent's own coord transforms.
  offset += parent->numCoordTransforms();
  // Skip coord transforms of all children before childIndex (pre-order traversal).
  for (size_t i = 0; i < childIndex; ++i) {
    FragmentProcessor::Iter iter(parent->childProcessor(i));
    while (const auto* fp = iter.next()) {
      offset += fp->numCoordTransforms();
    }
  }
  return offset;
}

// ---- Container FP expansion methods ----
// Compose and Xfermode recursively call emitModularFragProc() for children.
// GaussianBlur uses a hybrid approach: inline loop structure + legacy emitChild() for the
// child FP within the loop (due to coordFunc coordinate offset mechanism).

void ModularProgramBuilder::emitComposeFragmentProcessor(const FragmentProcessor* processor,
                                                         size_t transformedCoordVarsIdx,
                                                         const std::string& input,
                                                         const std::string& output) {
  auto numChildren = processor->numChildProcessors();
  std::string currentInput = input;
  for (size_t i = 0; i < numChildren; ++i) {
    auto childCoordIdx = childCoordVarsOffset(processor, transformedCoordVarsIdx, i);
    auto childOutput =
        emitModularFragProc(processor->childProcessor(i), childCoordIdx, currentInput);
    if (i == numChildren - 1) {
      fragmentShaderBuilder()->codeAppendf("%s = %s;", output.c_str(), childOutput.c_str());
    } else {
      currentInput = childOutput;
    }
  }
}

void ModularProgramBuilder::emitXfermodeFragmentProcessor(const FragmentProcessor* processor,
                                                          size_t transformedCoordVarsIdx,
                                                          const std::string& input,
                                                          const std::string& output) {
  auto xferFP = static_cast<const XfermodeFragmentProcessor*>(processor);
  auto fragBuilder = fragmentShaderBuilder();
  std::string coverage = "vec4(1.0)";
  auto inputRef = input.empty() ? std::string("vec4(1.0)") : input;

  if (xferFP->child == XfermodeFragmentProcessor::Child::TwoChild) {
    fragBuilder->codeAppendf("vec4 inputColor%s = vec4(%s.rgb, 1.0);",
                             programInfo->getMangledSuffix(processor).c_str(), inputRef.c_str());
    std::string opaqueInput = "inputColor" + programInfo->getMangledSuffix(processor);

    auto srcCoordIdx = childCoordVarsOffset(processor, transformedCoordVarsIdx, 0);
    auto srcColor = emitModularFragProc(processor->childProcessor(0), srcCoordIdx, opaqueInput);

    auto dstCoordIdx = childCoordVarsOffset(processor, transformedCoordVarsIdx, 1);
    auto dstColor = emitModularFragProc(processor->childProcessor(1), dstCoordIdx, opaqueInput);

    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(xferFP->mode));
    AppendMode(fragBuilder, srcColor, coverage, dstColor, output, xferFP->mode, false);
    fragBuilder->codeAppendf("%s *= %s.a;", output.c_str(), inputRef.c_str());
  } else {
    auto childCoordIdx = childCoordVarsOffset(processor, transformedCoordVarsIdx, 0);
    auto childColor = emitModularFragProc(processor->childProcessor(0), childCoordIdx, "");

    fragBuilder->codeAppendf("// Compose Xfer Mode: %s\n", BlendModeName(xferFP->mode));
    if (xferFP->child == XfermodeFragmentProcessor::Child::DstChild) {
      AppendMode(fragBuilder, inputRef, coverage, childColor, output, xferFP->mode, false);
    } else {
      AppendMode(fragBuilder, childColor, coverage, inputRef, output, xferFP->mode, false);
    }
  }
}

void ModularProgramBuilder::emitGaussianBlur1DFragmentProcessor(const FragmentProcessor* processor,
                                                                size_t transformedCoordVarsIdx,
                                                                const std::string& /*input*/,
                                                                const std::string& output) {
  auto blurFP = static_cast<const GaussianBlur1DFragmentProcessor*>(processor);
  auto fragBuilder = fragmentShaderBuilder();
  auto uHandler = uniformHandler();

  auto sigmaName = uHandler->addUniform("Sigma", UniformFormat::Float, ShaderStage::Fragment);
  auto texelSizeName = uHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());
  fragBuilder->codeAppendf("float sigma = %s;", sigmaName.c_str());
  fragBuilder->codeAppend("int radius = int(ceil(2.0 * sigma));");
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
  fragBuilder->codeAppend("float total = 0.0;");

  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", 4 * blurFP->maxSigma);
  fragBuilder->codeAppend("int i = j - radius;");
  fragBuilder->codeAppend("float weight = exp(-float(i*i) / (2.0*sigma*sigma));");
  fragBuilder->codeAppend("total += weight;");

  // Recursively emit child FP inside the loop with coordinate offset.
  auto childCoordIdx = childCoordVarsOffset(processor, transformedCoordVarsIdx, 0);
  auto childOutput = emitModularFragProc(
      processor->childProcessor(0), childCoordIdx, "vec4(1.0)",
      [](const std::string& coord) { return "(" + coord + " + offset * float(i))"; });

  fragBuilder->codeAppendf("sum += %s * weight;", childOutput.c_str());
  fragBuilder->codeAppend("if (i == radius) { break; }");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = sum / total;", output.c_str());
}

// ---- ClampedGradientEffect inline expansion ----

void ModularProgramBuilder::emitClampedGradientEffect(const FragmentProcessor* processor,
                                                      size_t transformedCoordVarsIdx,
                                                      const std::string& input,
                                                      const std::string& output) {
  auto clamped = static_cast<const ClampedGradientEffect*>(processor);
  auto fragBuilder = fragmentShaderBuilder();

  // Declare ClampedGradientEffect's own uniforms (leftBorderColor, rightBorderColor).
  auto leftBorderColorName =
      uniformHandler()->addUniform("leftBorderColor", UniformFormat::Float4, ShaderStage::Fragment);
  auto rightBorderColorName = uniformHandler()->addUniform(
      "rightBorderColor", UniformFormat::Float4, ShaderStage::Fragment);

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
    FragmentProcessor::TransformedCoordVars coords(gradLayout,
                                                   gradLayoutCoordIdx < transformedCoordVars.size()
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

  // Explicit dispatch by GP type — inlined from each GLSL*GeometryProcessor::emitCode().
  auto gpName = geometryProcessor->name();
  if (gpName == "DefaultGeometryProcessor") {
    auto gp = static_cast<const DefaultGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto matrixName =
        uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string positionName = "position";
    vertBuilder->codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                             matrixName.c_str(), gp->position.name().c_str());
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(gp->position));
    if (gp->aa == AAType::Coverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->coverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    args.vertBuilder->emitNormalizedPosition(positionName);

  } else if (gpName == "QuadPerEdgeAAGeometryProcessor") {
    auto gp = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto& uvCoordsVar = gp->uvCoord.empty() ? gp->position : gp->uvCoord;
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));
    if (gp->aa == AAType::Coverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->coverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    if (gp->commonColor.has_value()) {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    } else {
      auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), gp->color.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
    }
    args.vertBuilder->emitNormalizedPosition(gp->position.name());

  } else if (gpName == "EllipseGeometryProcessor") {
    auto gp = static_cast<const EllipseGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                             gp->inEllipseOffset.name().c_str());
    auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(),
                             gp->inEllipseRadii.name().c_str());
    auto fragBuilder = args.fragBuilder;
    if (gp->commonColor.has_value()) {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    } else {
      auto color = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), gp->inColor.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
    }
    args.vertBuilder->emitNormalizedPosition(gp->inPosition.name());
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                       ShaderVar(gp->inPosition));
    fragBuilder->codeAppendf("vec2 offset = %s.xy;", ellipseOffsets.fsIn().c_str());
    if (gp->stroke) {
      fragBuilder->codeAppendf("offset *= %s.xy;", ellipseRadii.fsIn().c_str());
    }
    fragBuilder->codeAppend("float test = dot(offset, offset) - 1.0;");
    fragBuilder->codeAppendf("vec2 grad = 2.0*offset*%s.xy;", ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("float grad_dot = dot(grad, grad);");
    fragBuilder->codeAppend("grad_dot = max(grad_dot, 1.1755e-38);");
    fragBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
    fragBuilder->codeAppend("float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");
    if (gp->stroke) {
      fragBuilder->codeAppendf("offset = %s.xy*%s.zw;", ellipseOffsets.fsIn().c_str(),
                               ellipseRadii.fsIn().c_str());
      fragBuilder->codeAppend("test = dot(offset, offset) - 1.0;");
      fragBuilder->codeAppendf("grad = 2.0*offset*%s.zw;", ellipseRadii.fsIn().c_str());
      fragBuilder->codeAppend("grad_dot = dot(grad, grad);");
      fragBuilder->codeAppend("invlen = inversesqrt(grad_dot);");
      fragBuilder->codeAppend("edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);");
    }
    fragBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.outputCoverage.c_str());

  } else if (gpName == "AtlasTextGeometryProcessor") {
    auto gp = static_cast<const AtlasTextGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto atlasName =
        uniformHandler->addUniform("atlasSizeInv", UniformFormat::Float2, ShaderStage::Vertex);
    auto samplerVarying = varyingHandler->addVarying("textureCoords", SLType::Float2);
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(gp->position));
    auto uvName = gp->maskCoord.name();
    vertBuilder->codeAppendf("%s = %s * %s;", samplerVarying.vsOut().c_str(), uvName.c_str(),
                             atlasName.c_str());
    if (gp->aa == AAType::Coverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->coverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    if (gp->commonColor.has_value()) {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    } else {
      auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), gp->color.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
    }
    auto textureView = gp->textureProxy->getTextureView();
    DEBUG_ASSERT(textureView != nullptr);
    DEBUG_ASSERT(textureView->getTexture() != nullptr);
    auto samplerHandle = uniformHandler->addSampler(textureView->getTexture(), "TextureSampler");
    fragBuilder->codeAppend("vec4 color = ");
    fragBuilder->appendTextureLookup(samplerHandle, samplerVarying.vsOut());
    fragBuilder->codeAppend(";");
    if (textureView->isAlphaOnly()) {
      fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
    } else {
      fragBuilder->codeAppendf("%s = clamp(vec4(color.rgb/color.a, 1.0), 0.0, 1.0);",
                               args.outputColor.c_str());
      fragBuilder->codeAppendf("%s = vec4(color.a);", args.outputCoverage.c_str());
    }
    args.vertBuilder->emitNormalizedPosition(gp->position.name());

  } else if (gpName == "MeshGeometryProcessor") {
    auto gp = static_cast<const MeshGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto matrixName =
        uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string positionName = "position";
    vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                             matrixName.c_str(), gp->position.name().c_str());
    if (gp->hasTexCoords) {
      auto texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
      vertBuilder->codeAppendf("%s = %s;", texCoordVar.vsOut().c_str(),
                               gp->texCoord.name().c_str());
      gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                         ShaderVar(gp->texCoord));
    } else {
      gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                         ShaderVar(gp->position));
    }
    if (gp->hasColors) {
      auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), gp->color.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
    } else {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    }
    if (gp->hasCoverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->coverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    vertBuilder->emitNormalizedPosition(positionName);

  } else if (gpName == "ShapeInstancedGeometryProcessor") {
    auto gp = static_cast<const ShapeInstancedGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto uvMatrixName =
        uniformHandler->addUniform("UVMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    vertBuilder->codeAppendf("highp vec2 local = (%s * vec3(%s, 1.0)).xy;", uvMatrixName.c_str(),
                             gp->position.name().c_str());
    auto viewMatrixName =
        uniformHandler->addUniform("ViewMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string positionName = "position";
    vertBuilder->codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy + %s;", positionName.c_str(),
                             viewMatrixName.c_str(), gp->position.name().c_str(),
                             gp->offset.name().c_str());
    ShaderVar localVar("local", SLType::Float2);
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, localVar);
    if (gp->aa == AAType::Coverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->coverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    if (gp->hasColors) {
      auto colorVar = varyingHandler->addVarying("InstanceColor", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(),
                               gp->instanceColor.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputColor.c_str());
    }
    vertBuilder->emitNormalizedPosition(positionName);

  } else if (gpName == "NonAARRectGeometryProcessor") {
    auto gp = static_cast<const NonAARRectGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    if (gp->commonColor.has_value()) {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    } else {
      auto color = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), gp->inColor.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
    }
    vertBuilder->emitNormalizedPosition(gp->inPosition.name());
    auto localCoordVarying = varyingHandler->addVarying("localCoord", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", localCoordVarying.vsOut().c_str(),
                             gp->inLocalCoord.name().c_str());
    auto radiiVarying = varyingHandler->addVarying("radii", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", radiiVarying.vsOut().c_str(), gp->inRadii.name().c_str());
    auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", boundsVarying.vsOut().c_str(),
                             gp->inRectBounds.name().c_str());
    Varying strokeWidthVarying;
    if (gp->stroke) {
      strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float2);
      vertBuilder->codeAppendf("%s = %s;", strokeWidthVarying.vsOut().c_str(),
                               gp->inStrokeWidth.name().c_str());
    }
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                       ShaderVar(gp->inPosition.name(), SLType::Float2));
    fragBuilder->codeAppendf("vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
    fragBuilder->codeAppendf("vec2 radii = %s;", radiiVarying.fsIn().c_str());
    fragBuilder->codeAppendf("vec4 bounds = %s;", boundsVarying.fsIn().c_str());
    fragBuilder->codeAppend("vec2 center = (bounds.xy + bounds.zw) * 0.5;");
    fragBuilder->codeAppend("vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;");
    fragBuilder->codeAppend("vec2 q = abs(localCoord - center) - halfSize + radii;");
    fragBuilder->codeAppend(
        "float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) + length(max(q / radii, 0.0)) - "
        "1.0;");
    fragBuilder->codeAppend("float outerCoverage = step(d, 0.0);");
    if (gp->stroke) {
      fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
      fragBuilder->codeAppend("vec2 innerHalfSize = halfSize - 2.0 * sw;");
      fragBuilder->codeAppend("vec2 innerRadii = max(radii - 2.0 * sw, vec2(0.0));");
      fragBuilder->codeAppend("float innerCoverage = 0.0;");
      fragBuilder->codeAppend("if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {");
      fragBuilder->codeAppend("  vec2 qi = abs(localCoord - center) - innerHalfSize + innerRadii;");
      fragBuilder->codeAppend("  vec2 safeInnerRadii = max(innerRadii, vec2(0.001));");
      fragBuilder->codeAppend(
          "  float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) + "
          "length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;");
      fragBuilder->codeAppend("  innerCoverage = step(di, 0.0);");
      fragBuilder->codeAppend("}");
      fragBuilder->codeAppend("float coverage = outerCoverage * (1.0 - innerCoverage);");
    } else {
      fragBuilder->codeAppend("float coverage = outerCoverage;");
    }
    fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());

  } else if (gpName == "HairlineLineGeometryProcessor") {
    auto gp = static_cast<const HairlineLineGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto matrixName =
        uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string positionName = "transformedPosition";
    vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                             matrixName.c_str(), gp->position.name().c_str());
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                       ShaderVar(positionName, SLType::Float2));
    auto edgeVarying = varyingHandler->addVarying("EdgeDistance", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", edgeVarying.vsOut().c_str(),
                             gp->edgeDistance.name().c_str());
    fragBuilder->codeAppendf("float edgeAlpha = abs(%s);", edgeVarying.fsIn().c_str());
    fragBuilder->codeAppend("edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);");
    if (gp->aaType != AAType::Coverage) {
      fragBuilder->codeAppend("edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;");
    }
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    auto coverageScale =
        uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = vec4(%s * edgeAlpha);", args.outputCoverage.c_str(),
                             coverageScale.c_str());
    vertBuilder->emitNormalizedPosition(positionName);

  } else if (gpName == "HairlineQuadGeometryProcessor") {
    auto gp = static_cast<const HairlineQuadGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto matrixName =
        uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
    std::string positionName = "transformedPosition";
    vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                             matrixName.c_str(), gp->position.name().c_str());
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                       ShaderVar(positionName, SLType::Float2));
    auto edgeVarying = varyingHandler->addVarying("HairQuadEdge", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", edgeVarying.vsOut().c_str(),
                             gp->hairQuadEdge.name().c_str());
    const char* edge = edgeVarying.fsIn().c_str();
    fragBuilder->codeAppendf("float edgeAlpha;");
    fragBuilder->codeAppendf("vec2 duvdx = vec2(dFdx(%s.xy));", edge);
    fragBuilder->codeAppendf("vec2 duvdy = vec2(dFdy(%s.xy));", edge);
    fragBuilder->codeAppendf(
        "vec2 gF = vec2(2.0 * %s.x * duvdx.x - duvdx.y,"
        "               2.0 * %s.x * duvdy.x - duvdy.y);",
        edge, edge);
    fragBuilder->codeAppendf("edgeAlpha = float(%s.x * %s.x - %s.y);", edge, edge, edge);
    fragBuilder->codeAppend("edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));");
    fragBuilder->codeAppend("edgeAlpha = max(1.0 - edgeAlpha, 0.0);");
    if (gp->aaType != AAType::Coverage) {
      fragBuilder->codeAppend("edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;");
    }
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    auto coverageScale =
        uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = vec4(%s * edgeAlpha);", args.outputCoverage.c_str(),
                             coverageScale.c_str());
    vertBuilder->emitNormalizedPosition(positionName);

  } else if (gpName == "RoundStrokeRectGeometryProcessor") {
    auto gp = static_cast<const RoundStrokeRectGeometryProcessor*>(geometryProcessor);
    auto vertBuilder = args.vertBuilder;
    auto fragBuilder = args.fragBuilder;
    auto varyingHandler = args.varyingHandler;
    auto uniformHandler = args.uniformHandler;
    varyingHandler->emitAttributes(*gp);
    auto& uvCoordsVar = gp->inUVCoord.empty() ? gp->inPosition : gp->inUVCoord;
    gp->emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));
    Varying ellipseRadii;
    if (gp->aaType == AAType::Coverage) {
      auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
      vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(),
                               gp->inCoverage.name().c_str());
      fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                               coverageVar.fsIn().c_str());
      ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float2);
      vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(),
                               gp->inEllipseRadii.name().c_str());
    } else {
      fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
    }
    auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                             gp->inEllipseOffset.name().c_str());
    if (gp->commonColor.has_value()) {
      auto colorName =
          uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
    } else {
      auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
      vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), gp->inColor.name().c_str());
      fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
    }
    fragBuilder->codeAppendf("vec2 offset = %s;", ellipseOffsets.fsIn().c_str());
    if (gp->aaType == AAType::Coverage) {
      fragBuilder->codeAppend("float test = dot(offset, offset) - 1.0;");
      fragBuilder->codeAppend("if (test > -0.5) {");
      fragBuilder->codeAppendf("vec2 grad = 2.0 * offset * %s;", ellipseRadii.fsIn().c_str());
      fragBuilder->codeAppend("float grad_dot = dot(grad, grad);");
      fragBuilder->codeAppend("grad_dot = max(grad_dot, 1.1755e-38);");
      fragBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
      fragBuilder->codeAppend("float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);");
      fragBuilder->codeAppendf("%s *= edgeAlpha;", args.outputCoverage.c_str());
      fragBuilder->codeAppendf("}");
    } else {
      fragBuilder->codeAppend("float test = dot(offset, offset);");
      fragBuilder->codeAppend("float edgeAlpha = step(test, 1.0);");
      fragBuilder->codeAppendf("%s *= edgeAlpha;", args.outputCoverage.c_str());
    }
    args.vertBuilder->emitNormalizedPosition(gp->inPosition.name());

  } else {
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

  // Inline dispatch by XP type.
  auto xpName = xferProcessor->name();
  if (xpName == "EmptyXferProcessor") {
    // EmptyXferProcessor: output = inputColor * inputCoverage
    auto fragBuilder = fragmentShaderBuilder();
    fragBuilder->codeAppendf("%s = %s * %s;", args.outputColor.c_str(), args.inputColor.c_str(),
                             args.inputCoverage.c_str());
  } else if (xpName == "PorterDuffXferProcessor") {
    auto xp = static_cast<const PorterDuffXferProcessor*>(xferProcessor);
    auto fragBuilder = fragmentShaderBuilder();
    auto uHandler = uniformHandler();
    const auto& dstColor = fragBuilder->dstColor();

    if (args.dstTextureSamplerHandle.isValid()) {
      fragBuilder->codeAppendf("if (%s.r <= 0.0 && %s.g <= 0.0 && %s.b <= 0.0) {",
                               args.inputCoverage.c_str(), args.inputCoverage.c_str(),
                               args.inputCoverage.c_str());
      fragBuilder->codeAppend("discard;");
      fragBuilder->codeAppend("}");

      auto dstTopLeftName =
          uHandler->addUniform("DstTextureUpperLeft", UniformFormat::Float2, ShaderStage::Fragment);
      auto dstCoordScaleName = uHandler->addUniform("DstTextureCoordScale", UniformFormat::Float2,
                                                    ShaderStage::Fragment);

      fragBuilder->codeAppend("// Read color from copy of the destination.\n");
      std::string dstTexCoord = "_dstTexCoord";
      fragBuilder->codeAppendf("vec2 %s = (gl_FragCoord.xy - %s) * %s;", dstTexCoord.c_str(),
                               dstTopLeftName.c_str(), dstCoordScaleName.c_str());

      fragBuilder->codeAppendf("vec4 %s = ", dstColor.c_str());
      fragBuilder->appendTextureLookup(args.dstTextureSamplerHandle, dstTexCoord);
      fragBuilder->codeAppend(";");
    }

    const char* outColor = "localOutputColor";
    fragBuilder->codeAppendf("vec4 %s;", outColor);
    AppendMode(fragBuilder, args.inputColor, args.inputCoverage, dstColor, outColor, xp->blendMode,
               true);

    if (!BlendModeAsCoeff(xp->blendMode, true)) {
      fragBuilder->codeAppendf("%s = %s * %s + (vec4(1.0) - %s) * %s;", outColor,
                               args.inputCoverage.c_str(), outColor, args.inputCoverage.c_str(),
                               dstColor.c_str());
    }
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), outColor);
  } else {
    // Safety fallback for any unhandled XP type.
    xferProcessor->emitCode(args);
  }

  fragmentShaderBuilder()->codeAppend("}");
  currentProcessors.pop_back();
}

}  // namespace tgfx
