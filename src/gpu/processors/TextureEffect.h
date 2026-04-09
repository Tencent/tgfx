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

#pragma once

#include "gpu/MangledResources.h"
#include "gpu/SamplerState.h"
#include "gpu/SamplingArgs.h"
#include "gpu/ShaderCallResult.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/YUVTextureView.h"

namespace tgfx {
class TextureEffect : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              std::shared_ptr<TextureProxy> proxy,
                                              const SamplingArgs& args = {},
                                              const Matrix* uvMatrix = nullptr,
                                              bool forceAsMask = false);

  static PlacementPtr<FragmentProcessor> MakeRGBAAA(BlockAllocator* allocator,
                                                    std::shared_ptr<TextureProxy> proxy,
                                                    const SamplingArgs& args,
                                                    const Point& alphaStart,
                                                    const Matrix* uvMatrix = nullptr);

  std::string name() const override {
    return "TextureEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  TextureEffect(std::shared_ptr<TextureProxy> proxy, const SamplingOptions& sampling,
                SrcRectConstraint constraint, const Point& alphaStart, const Matrix& uvMatrix,
                const std::optional<Rect>& subset);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    auto yuvTexture = getYUVTexture();
    if (yuvTexture) {
      macros.define("TGFX_TE_TEXTURE_MODE", yuvTexture->yuvFormat() == YUVFormat::I420 ? 1 : 2);
      if (IsLimitedYUVColorRange(yuvTexture->yuvColorSpace())) {
        macros.define("TGFX_TE_YUV_LIMITED_RANGE");
      }
    } else {
      macros.define("TGFX_TE_TEXTURE_MODE", 0);
    }
    if (alphaStart != Point::Zero()) {
      macros.define("TGFX_TE_RGBAAA");
    }
    if (textureProxy->isAlphaOnly()) {
      macros.define("TGFX_TE_ALPHA_ONLY");
    }
    if (needSubset()) {
      macros.define("TGFX_TE_SUBSET");
    }
    if (constraint == SrcRectConstraint::Strict) {
      macros.define("TGFX_TE_STRICT_CONSTRAINT");
    }
    if (coordTransform.matrix.hasPerspective()) {
      macros.define("TGFX_TE_PERSPECTIVE");
    }
    auto textureView = getTextureView();
    if (textureView && textureView->getTexture()->type() == TextureType::Rectangle) {
      macros.define("TGFX_TE_SAMPLER_TYPE", "sampler2DRect");
    }
  }

  std::string shaderFunctionFile() const override {
    return "fragment/texture_effect.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    if (needSubset()) {
      auto subsetName =
          uniformHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
      uniforms.add("Subset", subsetName);
    }
    // ExtraSubset uses the GP's subset varying (subsetVarName), not a uniform.
    // It is passed through MangledVaryings as "subsetVar" by emitLeafFPCall.
    if (alphaStart != Point::Zero()) {
      auto alphaStartName =
          uniformHandler->addUniform("AlphaStart", UniformFormat::Float2, ShaderStage::Fragment);
      uniforms.add("AlphaStart", alphaStartName);
    }
    if (getYUVTexture() != nullptr) {
      auto mat3Name = uniformHandler->addUniform("Mat3ColorConversion", UniformFormat::Float3x3,
                                                 ShaderStage::Fragment);
      uniforms.add("Mat3ColorConversion", mat3Name);
    }
  }

  ShaderCallResult buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& varyings,
                                      const MangledSamplers& samplers) const override {
    ShaderCallResult result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto input = inputColorVar.empty() ? "vec4(1.0)" : inputColorVar;
    auto coord = varyings.getCoordTransform(0);
    auto yuvTexture = getYUVTexture();
    std::string call =
        "vec4 " + result.outputVarName + " = TGFX_TextureEffect(" + input + ", " + coord + ", ";
    if (yuvTexture) {
      if (yuvTexture->yuvFormat() == YUVFormat::I420) {
        call += samplers.getByIndex(0) + ", " + samplers.getByIndex(1) + ", " +
                samplers.getByIndex(2) + ", " + uniforms.get("Mat3ColorConversion");
      } else {
        call += samplers.getByIndex(0) + ", " + samplers.getByIndex(1) + ", " +
                uniforms.get("Mat3ColorConversion");
      }
    } else {
      call += samplers.getByIndex(0);
    }
    if (needSubset()) {
      call += ", " + uniforms.get("Subset");
    }
    if (constraint == SrcRectConstraint::Strict) {
      call += ", " + varyings.get("subsetVar");
    }
    if (alphaStart != Point::Zero()) {
      call += ", " + uniforms.get("AlphaStart");
    }
    call += ");";
    result.statement = call;
    return result;
  }

  size_t onCountTextureSamplers() const override;

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t) const override {
    return samplerState;
  }

  TextureView* getTextureView() const;

  YUVTextureView* getYUVTexture() const;

  bool needSubset() const;

  std::shared_ptr<TextureProxy> textureProxy;
  SamplerState samplerState;
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
  Point alphaStart = {};
  CoordTransform coordTransform;
  std::optional<Rect> subset = std::nullopt;
};
}  // namespace tgfx
