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
#include "gpu/SamplingArgs.h"
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
  }

  std::string shaderFunctionFile() const override {
    return "fragment/texture_effect.frag";
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

  friend class ModularProgramBuilder;
};
}  // namespace tgfx
