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

  void onBuildShaderMacros(ShaderMacroSet& /*macros*/) const override {
    // Completely macro-free: all configuration passed as function parameters.
    // sampler2D/sampler2DRect handled via GLSL function overloading.
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
    int hasSubsetInt = needSubset() ? 1 : 0;
    int hasStrictInt = (constraint == SrcRectConstraint::Strict) ? 1 : 0;
    int hasRGBAAInt = (alphaStart != Point::Zero()) ? 1 : 0;
    int alphaOnlyInt = textureProxy->isAlphaOnly() ? 1 : 0;
    int yuvLimitedInt = (yuvTexture && IsLimitedYUVColorRange(yuvTexture->yuvColorSpace())) ? 1 : 0;
    std::string subsetArg = hasSubsetInt ? uniforms.get("Subset") : "vec4(0.0)";
    std::string extraSubsetArg = hasStrictInt ? varyings.get("subsetVar") : "vec4(0.0)";
    std::string alphaStartArg = hasRGBAAInt ? uniforms.get("AlphaStart") : "vec2(0.0)";
    std::string call = "vec4 " + result.outputVarName + " = ";
    if (yuvTexture) {
      if (yuvTexture->yuvFormat() == YUVFormat::I420) {
        call += "TGFX_TextureEffect_I420(" + input + ", " + coord + ", " + samplers.getByIndex(0) +
                ", " + samplers.getByIndex(1) + ", " + samplers.getByIndex(2) + ", " +
                uniforms.get("Mat3ColorConversion") + ", " + subsetArg + ", " + extraSubsetArg +
                ", " + alphaStartArg + ", " + std::to_string(hasSubsetInt) + ", " +
                std::to_string(hasStrictInt) + ", " + std::to_string(hasRGBAAInt) + ", " +
                std::to_string(yuvLimitedInt) + ");";
      } else {
        call += "TGFX_TextureEffect_NV12(" + input + ", " + coord + ", " + samplers.getByIndex(0) +
                ", " + samplers.getByIndex(1) + ", " + uniforms.get("Mat3ColorConversion") + ", " +
                subsetArg + ", " + extraSubsetArg + ", " + alphaStartArg + ", " +
                std::to_string(hasSubsetInt) + ", " + std::to_string(hasStrictInt) + ", " +
                std::to_string(hasRGBAAInt) + ", " + std::to_string(yuvLimitedInt) + ");";
      }
    } else {
      call += "TGFX_TextureEffect_RGBA(" + input + ", " + coord + ", " + samplers.getByIndex(0) +
              ", " + subsetArg + ", " + extraSubsetArg + ", " + alphaStartArg + ", " +
              std::to_string(hasSubsetInt) + ", " + std::to_string(hasStrictInt) + ", " +
              std::to_string(hasRGBAAInt) + ", " + std::to_string(alphaOnlyInt) + ");";
    }
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
