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
#include "gpu/ShaderCallManifest.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class TiledTextureEffect : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              std::shared_ptr<TextureProxy> textureProxy,
                                              const SamplingArgs& args,
                                              const Matrix* uvMatrix = nullptr,
                                              bool forceAsMask = false);

  std::string name() const override {
    return "TiledTextureEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  enum class ShaderMode {
    None,                 // Using HW mode
    Clamp,                // Shader based clamp, no filter specialization
    RepeatNearestNone,    // Simple repeat for nearest sampling, no mipmapping.
    RepeatLinearNone,     // Filter the subset boundary for kRepeat mode, no mip mapping
    RepeatLinearMipmap,   // Logic for linear filtering and LOD selection with kRepeat mode.
    RepeatNearestMipmap,  // Logic for nearest filtering and LOD selection with kRepeat mode.
    MirrorRepeat,         // Mirror repeat (doesn't depend on filter))
    ClampToBorderNearest,
    ClampToBorderLinear
  };

  struct Sampling {
    Sampling(const TextureView* textureView, SamplerState sampler, const Rect& subset);

    SamplerState hwSampler;
    ShaderMode shaderModeX = ShaderMode::None;
    ShaderMode shaderModeY = ShaderMode::None;
    Rect shaderSubset = {};
    Rect shaderClamp = {};
  };

  TiledTextureEffect(std::shared_ptr<TextureProxy> proxy, const SamplerState& samplerState,
                     SrcRectConstraint constraint, const Matrix& uvMatrix,
                     const std::optional<Rect>& subset);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& /*macros*/) const override {
    // Completely macro-free: all configuration passed as function parameters.
    // sampler2D/sampler2DRect handled via GLSL function overloading.
  }

  std::string shaderFunctionFile() const override {
    return "fragment/tiled_texture_effect.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto textureView = getTextureView();
    if (textureView == nullptr) {
      return;
    }
    Sampling sampling(textureView, samplerState, subset);
    auto modeX = static_cast<int>(sampling.shaderModeX);
    auto modeY = static_cast<int>(sampling.shaderModeY);
    bool usesSubset = (modeX != static_cast<int>(ShaderMode::None) &&
                       modeX != static_cast<int>(ShaderMode::Clamp) &&
                       modeX != static_cast<int>(ShaderMode::ClampToBorderLinear)) ||
                      (modeY != static_cast<int>(ShaderMode::None) &&
                       modeY != static_cast<int>(ShaderMode::Clamp) &&
                       modeY != static_cast<int>(ShaderMode::ClampToBorderLinear));
    if (usesSubset) {
      auto subsetName =
          uniformHandler->addUniform("Subset", UniformFormat::Float4, ShaderStage::Fragment);
      uniforms.add("Subset", subsetName);
    }
    bool usesClampX = (modeX != static_cast<int>(ShaderMode::None) &&
                       modeX != static_cast<int>(ShaderMode::ClampToBorderNearest));
    bool usesClampY = (modeY != static_cast<int>(ShaderMode::None) &&
                       modeY != static_cast<int>(ShaderMode::ClampToBorderNearest));
    if (usesClampX || usesClampY) {
      auto clampName =
          uniformHandler->addUniform("Clamp", UniformFormat::Float4, ShaderStage::Fragment);
      uniforms.add("Clamp", clampName);
    }
    auto requiresUnorm = [](int mode) {
      return mode != static_cast<int>(ShaderMode::None) &&
             mode != static_cast<int>(ShaderMode::Clamp) &&
             mode != static_cast<int>(ShaderMode::RepeatNearestNone) &&
             mode != static_cast<int>(ShaderMode::MirrorRepeat);
    };
    bool mustNormalize = textureView->getTexture()->type() != TextureType::Rectangle;
    if ((requiresUnorm(modeX) || requiresUnorm(modeY)) && mustNormalize) {
      auto dimensionName =
          uniformHandler->addUniform("Dimension", UniformFormat::Float2, ShaderStage::Fragment);
      uniforms.add("Dimension", dimensionName);
    }
    if (constraint == SrcRectConstraint::Strict) {
      // ExtraSubset uses the GP's subset varying, not a uniform.
    }
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& varyings,
                                      const MangledSamplers& samplers) const override {
    ShaderCallManifest result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto input = inputColorVar.empty() ? "vec4(1.0)" : inputColorVar;
    auto coord = varyings.getCoordTransform(0);
    auto textureView = getTextureView();
    int modeXInt = 0;
    int modeYInt = 0;
    bool usesSubset = false;
    bool usesClamp = false;
    bool hasDim = false;
    if (textureView != nullptr) {
      Sampling sampling(textureView, samplerState, subset);
      modeXInt = static_cast<int>(sampling.shaderModeX);
      modeYInt = static_cast<int>(sampling.shaderModeY);
      usesSubset = (modeXInt != static_cast<int>(ShaderMode::None) &&
                    modeXInt != static_cast<int>(ShaderMode::Clamp) &&
                    modeXInt != static_cast<int>(ShaderMode::ClampToBorderLinear)) ||
                   (modeYInt != static_cast<int>(ShaderMode::None) &&
                    modeYInt != static_cast<int>(ShaderMode::Clamp) &&
                    modeYInt != static_cast<int>(ShaderMode::ClampToBorderLinear));
      bool usesClampX = (modeXInt != static_cast<int>(ShaderMode::None) &&
                         modeXInt != static_cast<int>(ShaderMode::ClampToBorderNearest));
      bool usesClampY = (modeYInt != static_cast<int>(ShaderMode::None) &&
                         modeYInt != static_cast<int>(ShaderMode::ClampToBorderNearest));
      usesClamp = usesClampX || usesClampY;
      auto requiresUnorm = [](int mode) {
        return mode != static_cast<int>(ShaderMode::None) &&
               mode != static_cast<int>(ShaderMode::Clamp) &&
               mode != static_cast<int>(ShaderMode::RepeatNearestNone) &&
               mode != static_cast<int>(ShaderMode::MirrorRepeat);
      };
      bool mustNormalize = textureView->getTexture()->type() != TextureType::Rectangle;
      hasDim = (requiresUnorm(modeXInt) || requiresUnorm(modeYInt)) && mustNormalize;
    }
    int alphaOnlyInt = textureProxy->isAlphaOnly() ? 1 : 0;
    int hasStrictInt = (constraint == SrcRectConstraint::Strict) ? 1 : 0;
    std::string subsetArg = usesSubset ? uniforms.get("Subset") : "vec4(0.0)";
    std::string clampArg = usesClamp ? uniforms.get("Clamp") : "vec4(0.0)";
    std::string dimArg = hasDim ? uniforms.get("Dimension") : "vec2(1.0)";
    std::string extraSubsetArg = hasStrictInt ? varyings.get("subsetVar") : "vec4(0.0)";
    std::string call =
        "vec4 " + result.outputVarName + " = TGFX_TiledTextureEffect(" + input + ", " + coord +
        ", " + samplers.getByIndex(0) + ", " + subsetArg + ", " + clampArg + ", " + dimArg + ", " +
        extraSubsetArg + ", " + std::to_string(modeXInt) + ", " + std::to_string(modeYInt) + ", " +
        std::to_string(alphaOnlyInt) + ", " + std::to_string(hasDim ? 1 : 0) + ", " +
        std::to_string(usesSubset ? 1 : 0) + ", " + std::to_string(usesClamp ? 1 : 0) + ", " +
        std::to_string(hasStrictInt) + ");";
    result.statement = call;
    return result;
  }

  size_t onCountTextureSamplers() const override;

  std::shared_ptr<Texture> onTextureAt(size_t) const override;

  SamplerState onSamplerStateAt(size_t) const override;

  const TextureView* getTextureView() const;

  static ShaderMode GetShaderMode(TileMode tileMode, FilterMode filter, MipmapMode mipmapMode);

  std::shared_ptr<TextureProxy> textureProxy;
  SamplerState samplerState;
  CoordTransform coordTransform;
  Rect subset = Rect::MakeEmpty();
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
};
}  // namespace tgfx
