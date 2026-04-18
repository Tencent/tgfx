/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ColorSpaceXFormEffect.h"
#include <skcms.h>

namespace tgfx {

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(BlockAllocator* allocator,
                                                            ColorSpace* src, AlphaType srcAT,
                                                            ColorSpace* dst, AlphaType dstAT) {
  return Make(allocator, std::make_shared<ColorSpaceXformSteps>(src, srcAT, dst, dstAT));
}

PlacementPtr<FragmentProcessor> ColorSpaceXformEffect::Make(
    BlockAllocator* allocator, std::shared_ptr<ColorSpaceXformSteps> colorXform) {
  return allocator->make<ColorSpaceXformEffect>(std::move(colorXform));
}

void ColorSpaceXformEffect::onBuildShaderMacros(ShaderMacroSet& macros) const {
  auto* steps = colorSpaceXformSteps.get();
  if (steps->flags.unPremul) {
    macros.define("TGFX_CSX_UNPREMUL");
  }
  if (steps->flags.linearize) {
    macros.define("TGFX_CSX_SRC_TF");
    macros.define(
        "TGFX_CSX_SRC_TF_TYPE",
        static_cast<int>(gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->srcTransferFunction))));
  }
  if (steps->flags.srcOOTF) {
    macros.define("TGFX_CSX_SRC_OOTF");
  }
  if (steps->flags.gamutTransform) {
    macros.define("TGFX_CSX_GAMUT_XFORM");
  }
  if (steps->flags.dstOOTF) {
    macros.define("TGFX_CSX_DST_OOTF");
  }
  if (steps->flags.encode) {
    macros.define("TGFX_CSX_DST_TF");
    macros.define("TGFX_CSX_DST_TF_TYPE", static_cast<int>(gfx::skcms_TransferFunction_getType(
                                              reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                                  &steps->dstTransferFunctionInverse))));
  }
  if (steps->flags.premul) {
    macros.define("TGFX_CSX_PREMUL");
  }
}

void ColorSpaceXformEffect::declareResources(UniformHandler* uniformHandler,
                                             MangledUniforms& uniforms,
                                             MangledSamplers& /*samplers*/) const {
  auto* steps = colorSpaceXformSteps.get();
  if (steps->flags.linearize) {
    uniforms.add("SrcTF0", uniformHandler->addUniform("SrcTF0", UniformFormat::Float4,
                                                      ShaderStage::Fragment));
    uniforms.add("SrcTF1", uniformHandler->addUniform("SrcTF1", UniformFormat::Float4,
                                                      ShaderStage::Fragment));
  }
  if (steps->flags.srcOOTF) {
    uniforms.add("SrcOOTF", uniformHandler->addUniform("SrcOOTF", UniformFormat::Float4,
                                                       ShaderStage::Fragment));
  }
  if (steps->flags.gamutTransform) {
    uniforms.add("ColorXform", uniformHandler->addUniform("ColorXform", UniformFormat::Float3x3,
                                                          ShaderStage::Fragment));
  }
  if (steps->flags.dstOOTF) {
    uniforms.add("DstOOTF", uniformHandler->addUniform("DstOOTF", UniformFormat::Float4,
                                                       ShaderStage::Fragment));
  }
  if (steps->flags.encode) {
    uniforms.add("DstTF0", uniformHandler->addUniform("DstTF0", UniformFormat::Float4,
                                                      ShaderStage::Fragment));
    uniforms.add("DstTF1", uniformHandler->addUniform("DstTF1", UniformFormat::Float4,
                                                      ShaderStage::Fragment));
  }
}

ShaderCallResult ColorSpaceXformEffect::buildCallStatement(
    const std::string& inputColorVar, int /*fpIndex*/, const MangledUniforms& uniforms,
    const MangledVaryings& /*varyings*/, const MangledSamplers& /*samplers*/) const {
  auto* steps = colorSpaceXformSteps.get();
  auto input = inputColorVar.empty() ? std::string("vec4(1.0)") : inputColorVar;
  std::string srcTF0 = steps->flags.linearize ? uniforms.get("SrcTF0") : "vec4(0.0)";
  std::string srcTF1 = steps->flags.linearize ? uniforms.get("SrcTF1") : "vec4(0.0)";
  std::string srcOOTF = steps->flags.srcOOTF ? uniforms.get("SrcOOTF") : "vec4(0.0)";
  std::string colorXform = steps->flags.gamutTransform ? uniforms.get("ColorXform") : "mat3(1.0)";
  std::string dstOOTF = steps->flags.dstOOTF ? uniforms.get("DstOOTF") : "vec4(0.0)";
  std::string dstTF0 = steps->flags.encode ? uniforms.get("DstTF0") : "vec4(0.0)";
  std::string dstTF1 = steps->flags.encode ? uniforms.get("DstTF1") : "vec4(0.0)";

  ShaderCallResult result;
  result.statement = "vec4 _csxResult = TGFX_ColorSpaceXformEffect(" + input + ", " + srcTF0 +
                     ", " + srcTF1 + ", " + srcOOTF + ", " + colorXform + ", " + dstOOTF + ", " +
                     dstTF0 + ", " + dstTF1 + ");\n";
  result.outputVarName = "_csxResult";
  return result;
}

void ColorSpaceXformEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t key = ColorSpaceXformSteps::XFormKey(colorSpaceXformSteps.get());
  bytesKey->write(key);
}

ColorSpaceXformEffect::ColorSpaceXformEffect(std::shared_ptr<ColorSpaceXformSteps> colorXform)
    : FragmentProcessor(ClassID()), colorSpaceXformSteps(std::move(colorXform)) {
}

void ColorSpaceXformEffect::onSetData(UniformData* /*vertexUniformData*/,
                                      UniformData* fragmentUniformData) const {
  ColorSpaceXformHelper helper{};
  helper.setData(fragmentUniformData, colorSpaceXformSteps.get());
}
}  // namespace tgfx
