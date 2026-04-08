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
#include "gpu/ShaderCallResult.h"
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

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    auto textureView = getTextureView();
    if (textureView == nullptr) {
      return;
    }
    Sampling sampling(textureView, samplerState, subset);
    macros.define("TGFX_TTE_MODE_X", static_cast<int>(sampling.shaderModeX));
    macros.define("TGFX_TTE_MODE_Y", static_cast<int>(sampling.shaderModeY));
    if (constraint == SrcRectConstraint::Strict) {
      macros.define("TGFX_TTE_STRICT_CONSTRAINT");
    }
    if (textureProxy->isAlphaOnly()) {
      macros.define("TGFX_TTE_ALPHA_ONLY");
    }
    if (coordTransform.matrix.hasPerspective()) {
      macros.define("TGFX_TTE_PERSPECTIVE");
    }
  }

  std::string shaderFunctionFile() const override {
    return "fragment/tiled_texture_effect.frag";
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
    std::string call = "vec4 " + result.outputVarName + " = TGFX_TiledTextureEffect(" + input +
                       ", " + coord + ", " + samplers.getByIndex(0);
    auto textureView = getTextureView();
    if (textureView != nullptr) {
      Sampling sampling(textureView, samplerState, subset);
      auto modeX = static_cast<int>(sampling.shaderModeX);
      auto modeY = static_cast<int>(sampling.shaderModeY);
      bool usesSubset = (modeX != static_cast<int>(ShaderMode::None) &&
                         modeX != static_cast<int>(ShaderMode::Clamp) &&
                         modeX != static_cast<int>(ShaderMode::ClampToBorderLinear)) ||
                        (modeY != static_cast<int>(ShaderMode::None) &&
                         modeY != static_cast<int>(ShaderMode::Clamp) &&
                         modeY != static_cast<int>(ShaderMode::ClampToBorderLinear));
      bool usesClampX = (modeX != static_cast<int>(ShaderMode::None) &&
                         modeX != static_cast<int>(ShaderMode::ClampToBorderNearest));
      bool usesClampY = (modeY != static_cast<int>(ShaderMode::None) &&
                         modeY != static_cast<int>(ShaderMode::ClampToBorderNearest));
      bool usesClamp = usesClampX || usesClampY;
      if (usesSubset) {
        call += ", " + uniforms.get("Subset");
      }
      if (usesClamp) {
        call += ", " + uniforms.get("Clamp");
      }
      auto requiresUnorm = [](int mode) {
        return mode != static_cast<int>(ShaderMode::None) &&
               mode != static_cast<int>(ShaderMode::Clamp) &&
               mode != static_cast<int>(ShaderMode::RepeatNearestNone) &&
               mode != static_cast<int>(ShaderMode::MirrorRepeat);
      };
      bool unormRequired = requiresUnorm(modeX) || requiresUnorm(modeY);
      bool mustNormalize = textureView->getTexture()->type() != TextureType::Rectangle;
      if (unormRequired && mustNormalize) {
        call += ", " + uniforms.get("Dimension");
      }
    }
    call += ");";
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

  friend class ModularProgramBuilder;
};
}  // namespace tgfx
