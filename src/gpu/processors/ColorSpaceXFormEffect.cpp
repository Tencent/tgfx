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
#include <functional>
#include "gpu/ShaderMacroSet.h"

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

void ColorSpaceXformEffect::BuildMacros(bool unPremul, bool srcTF, int srcTFType, bool srcOOTF,
                                        bool gamutXform, bool dstOOTF, bool dstTF, int dstTFType,
                                        bool premul, ShaderMacroSet& macros) {
  if (unPremul) {
    macros.define("TGFX_CSX_UNPREMUL");
  }
  if (srcTF) {
    macros.define("TGFX_CSX_SRC_TF");
    macros.define("TGFX_CSX_SRC_TF_TYPE", srcTFType);
  }
  if (srcOOTF) {
    macros.define("TGFX_CSX_SRC_OOTF");
  }
  if (gamutXform) {
    macros.define("TGFX_CSX_GAMUT_XFORM");
  }
  if (dstOOTF) {
    macros.define("TGFX_CSX_DST_OOTF");
  }
  if (dstTF) {
    macros.define("TGFX_CSX_DST_TF");
    macros.define("TGFX_CSX_DST_TF_TYPE", dstTFType);
  }
  if (premul) {
    macros.define("TGFX_CSX_PREMUL");
  }
}

void ColorSpaceXformEffect::onBuildShaderMacros(ShaderMacroSet& macros) const {
  auto* steps = colorSpaceXformSteps.get();
  int srcTFType = 0;
  int dstTFType = 0;
  if (steps->flags.linearize) {
    srcTFType = static_cast<int>(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->srcTransferFunction)));
  }
  if (steps->flags.encode) {
    dstTFType = static_cast<int>(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->dstTransferFunctionInverse)));
  }
  BuildMacros(steps->flags.unPremul, steps->flags.linearize, srcTFType, steps->flags.srcOOTF,
              steps->flags.gamutTransform, steps->flags.dstOOTF, steps->flags.encode, dstTFType,
              steps->flags.premul, macros);
}

std::vector<ShaderVariant> ColorSpaceXformEffect::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(static_cast<size_t>(kVariantCount));
  std::hash<std::string> hasher;
  int index = 0;
  // Iteration order matches the bit layout documented in the header.
  for (int dstTFBits = 0; dstTFBits < kTFValueCount; ++dstTFBits) {
    for (int srcTFBits = 0; srcTFBits < kTFValueCount; ++srcTFBits) {
      for (int premul = 0; premul < 2; ++premul) {
        for (int dstOOTF = 0; dstOOTF < 2; ++dstOOTF) {
          for (int gamut = 0; gamut < 2; ++gamut) {
            for (int srcOOTF = 0; srcOOTF < 2; ++srcOOTF) {
              for (int unPremul = 0; unPremul < 2; ++unPremul) {
                bool srcTF = srcTFBits != 0;
                bool dstTF = dstTFBits != 0;
                int srcTFType = srcTFBits > 0 ? srcTFBits - 1 : 0;
                int dstTFType = dstTFBits > 0 ? dstTFBits - 1 : 0;
                ShaderMacroSet macros;
                BuildMacros(unPremul != 0, srcTF, srcTFType, srcOOTF != 0, gamut != 0, dstOOTF != 0,
                            dstTF, dstTFType, premul != 0, macros);
                ShaderVariant variant;
                variant.index = index++;
                variant.name =
                    "ColorSpaceXformEffect[unPremul=" + std::to_string(unPremul) +
                    ",srcTF=" + std::to_string(srcTFBits) + ",srcOOTF=" + std::to_string(srcOOTF) +
                    ",gamut=" + std::to_string(gamut) + ",dstOOTF=" + std::to_string(dstOOTF) +
                    ",dstTF=" + std::to_string(dstTFBits) + ",premul=" + std::to_string(premul) +
                    "]";
                variant.preamble = macros.toPreamble();
                variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
                variants.emplace_back(std::move(variant));
              }
            }
          }
        }
      }
    }
  }
  return variants;
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

ShaderCallManifest ColorSpaceXformEffect::buildCallStatement(
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

  ShaderCallManifest manifest;
  manifest.functionName = "TGFX_ColorSpaceXformEffect";
  manifest.outputVarName = "_csxResult";
  manifest.argExpressions = {input, srcTF0, srcTF1, srcOOTF, colorXform, dstOOTF, dstTF0, dstTF1};
  return manifest;
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
