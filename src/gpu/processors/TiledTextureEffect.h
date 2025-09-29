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

#include "gpu/SamplerState.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class TiledTextureEffect : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(std::shared_ptr<TextureProxy> textureProxy,
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

  size_t onCountTextureSamplers() const override;

  GPUTexture* onTextureAt(size_t) const override;

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
