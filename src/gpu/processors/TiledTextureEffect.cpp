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

#include "TiledTextureEffect.h"
#include "ConstColorProcessor.h"
#include "TextureEffect.h"
#include "core/utils/MathExtra.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
TiledTextureEffect::ShaderMode TiledTextureEffect::GetShaderMode(TileMode tileMode,
                                                                 FilterMode filter,
                                                                 MipmapMode mipmapMode) {
  switch (tileMode) {
    case TileMode::Mirror:
      return ShaderMode::MirrorRepeat;
    case TileMode::Clamp:
      return ShaderMode::Clamp;
    case TileMode::Repeat:
      switch (mipmapMode) {
        case MipmapMode::None:
          switch (filter) {
            case FilterMode::Nearest:
              return ShaderMode::RepeatNearestNone;
            case FilterMode::Linear:
              return ShaderMode::RepeatLinearNone;
          }
        case MipmapMode::Nearest:
        case MipmapMode::Linear:
          switch (filter) {
            case FilterMode::Nearest:
              return ShaderMode::RepeatNearestMipmap;
            case FilterMode::Linear:
              return ShaderMode::RepeatLinearMipmap;
          }
      }
    case TileMode::Decal:
      switch (filter) {
        case FilterMode::Nearest:
          return ShaderMode::ClampToBorderNearest;
        case FilterMode::Linear:
          return ShaderMode::ClampToBorderLinear;
      }
  }
}

TiledTextureEffect::Sampling::Sampling(const TextureView* textureView, SamplerState sampler,
                                       const Rect& subset) {
  struct Span {
    float a = 0.f;
    float b = 0.f;

    Span makeInset(float o) const {
      Span r = {a + o, b - o};
      if (r.a > r.b) {
        r.a = r.b = (r.a + r.b) / 2;
      }
      return r;
    }
  };
  struct Result1D {
    ShaderMode shaderMode = ShaderMode::None;
    Span shaderSubset;
    Span shaderClamp;
    TileMode hwMode = TileMode::Clamp;
  };
  auto caps = textureView->getContext()->caps();
  auto canDoWrapInHW = [&](int size, TileMode tileMode) {
    if (tileMode == TileMode::Decal && !caps->clampToBorderSupport) {
      return false;
    }
    if (tileMode != TileMode::Clamp && !caps->npotTextureTileSupport && !IsPow2(size)) {
      return false;
    }
    if (textureView->getTexture()->type() != GPUTextureType::TwoD &&
        !(tileMode == TileMode::Clamp || tileMode == TileMode::Decal)) {
      return false;
    }
    return true;
  };
  auto resolve = [&](int size, TileMode tileMode, Span subsetSpan, float linearFilterInset) {
    Result1D r;
    bool canDoModeInHW = canDoWrapInHW(size, tileMode);
    if (canDoModeInHW && subsetSpan.a <= 0 && subsetSpan.b >= static_cast<float>(size)) {
      r.hwMode = tileMode;
      return r;
    }
    r.shaderSubset = subsetSpan;
    if (sampler.filterMode == FilterMode::Nearest) {
      Span isubset{std::floor(subsetSpan.a), std::ceil(subsetSpan.b)};
      // This inset prevents sampling neighboring texels that could occur when
      // texture coords fall exactly at texel boundaries (depending on precision
      // and GPU-specific snapping at the boundary).
      r.shaderClamp = isubset.makeInset(0.5f);
    } else {
      r.shaderClamp = subsetSpan.makeInset(linearFilterInset);
    }
    auto mipmapMode = textureView->hasMipmaps() ? sampler.mipmapMode : MipmapMode::None;
    r.shaderMode = GetShaderMode(tileMode, sampler.filterMode, mipmapMode);
    DEBUG_ASSERT(r.shaderMode != ShaderMode::None);
    return r;
  };

  Span subsetX{subset.left, subset.right};
  auto x = resolve(textureView->width(), sampler.tileModeX, subsetX, 0.5f);
  Span subsetY{subset.top, subset.bottom};
  auto y = resolve(textureView->height(), sampler.tileModeY, subsetY, 0.5f);
  hwSampler = SamplerState(x.hwMode, y.hwMode, sampler.filterMode, sampler.mipmapMode);
  shaderModeX = x.shaderMode;
  shaderModeY = y.shaderMode;
  shaderSubset = {x.shaderSubset.a, y.shaderSubset.a, x.shaderSubset.b, y.shaderSubset.b};
  shaderClamp = {x.shaderClamp.a, y.shaderClamp.a, x.shaderClamp.b, y.shaderClamp.b};
}

TiledTextureEffect::TiledTextureEffect(std::shared_ptr<TextureProxy> proxy,
                                       const SamplerState& samplerState,
                                       SrcRectConstraint constraint, const Matrix& uvMatrix,
                                       const std::optional<Rect>& subset)
    : FragmentProcessor(ClassID()), textureProxy(std::move(proxy)), samplerState(samplerState),
      coordTransform(uvMatrix, textureProxy.get()),
      subset(subset.value_or(Rect::MakeWH(textureProxy->width(), textureProxy->height()))),
      constraint(constraint) {
  addCoordTransform(&coordTransform);
}

void TiledTextureEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return;
  }
  // Sometimes textureProxy->isAlphaOnly() != texture->isAlphaOnly(), we use
  // textureProxy->isAlphaOnly() to determine the alpha-only flag.
  bytesKey->write(textureProxy->isAlphaOnly());
  Sampling sampling(textureView, samplerState, subset);
  auto flags = static_cast<uint32_t>(sampling.shaderModeX);
  flags |= static_cast<uint32_t>(sampling.shaderModeY) << 4;
  flags |= constraint == SrcRectConstraint::Strict ? static_cast<uint32_t>(1) << 8 : 0;
  bytesKey->write(flags);
}

size_t TiledTextureEffect::onCountTextureSamplers() const {
  auto textureView = getTextureView();
  return textureView ? 1 : 0;
}

std::shared_ptr<GPUTexture> TiledTextureEffect::onTextureAt(size_t) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  return textureView->getTexture();
}

SamplerState TiledTextureEffect::onSamplerStateAt(size_t) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return {};
  }
  Sampling sampling(textureView, samplerState, subset);
  return sampling.hwSampler;
}

const TextureView* TiledTextureEffect::getTextureView() const {
  auto textureView = textureProxy->getTextureView().get();
  if (textureView && !textureView->isYUV()) {
    return textureView;
  }
  return nullptr;
}
}  // namespace tgfx
