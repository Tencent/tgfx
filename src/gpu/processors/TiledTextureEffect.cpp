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
using Wrap = SamplerState::WrapMode;

TiledTextureEffect::ShaderMode TiledTextureEffect::GetShaderMode(Wrap wrap, FilterMode filter,
                                                                 MipmapMode mm) {
  switch (wrap) {
    case Wrap::MirrorRepeat:
      return ShaderMode::MirrorRepeat;
    case Wrap::Clamp:
      return ShaderMode::Clamp;
    case Wrap::Repeat:
      switch (mm) {
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
    case Wrap::ClampToBorder:
      switch (filter) {
        case FilterMode::Nearest:
          return ShaderMode::ClampToBorderNearest;
        case FilterMode::Linear:
          return ShaderMode::ClampToBorderLinear;
      }
  }
}

TiledTextureEffect::Sampling::Sampling(const Texture* texture, SamplerState sampler,
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
    Wrap hwWrap = Wrap::Clamp;
  };
  auto caps = texture->getContext()->caps();
  auto canDoWrapInHW = [&](int size, Wrap wrap) {
    if (wrap == Wrap::ClampToBorder && !caps->clampToBorderSupport) {
      return false;
    }
    if (wrap != Wrap::Clamp && !caps->npotTextureTileSupport && !IsPow2(size)) {
      return false;
    }
    if (texture->getSampler()->type() != SamplerType::TwoD &&
        !(wrap == Wrap::Clamp || wrap == Wrap::ClampToBorder)) {
      return false;
    }
    return true;
  };
  auto resolve = [&](int size, Wrap wrap, Span subset, SamplerState samplerState,
                     float linearFilterInset) {
    Result1D r;
    bool canDoModeInHW = canDoWrapInHW(size, wrap);
    if (canDoModeInHW && subset.a <= 0 && subset.b >= static_cast<float>(size)) {
      r.hwWrap = wrap;
      return r;
    }
    r.shaderSubset = subset;
    if (samplerState.filterMode == FilterMode::Nearest) {
      Span isubset{std::floor(subset.a), std::ceil(subset.b)};
      // This inset prevents sampling neighboring texels that could occur when
      // texture coords fall exactly at texel boundaries (depending on precision
      // and GPU-specific snapping at the boundary).
      r.shaderClamp = isubset.makeInset(0.5f);
    } else {
      r.shaderClamp = subset.makeInset(linearFilterInset);
    }
    auto mipmapMode = texture->hasMipmaps() ? sampler.mipmapMode : MipmapMode::None;
    r.shaderMode = GetShaderMode(wrap, sampler.filterMode, mipmapMode);
    DEBUG_ASSERT(r.shaderMode != ShaderMode::None);
    return r;
  };

  Span subsetX{subset.left, subset.right};
  auto x = resolve(texture->width(), sampler.wrapModeX, subsetX, sampler, 0.5f);
  Span subsetY{subset.top, subset.bottom};
  auto y = resolve(texture->height(), sampler.wrapModeY, subsetY, sampler, 0.5f);
  hwSampler = SamplerState(x.hwWrap, y.hwWrap, sampler.filterMode, sampler.mipmapMode);
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
  auto texture = getTexture();
  if (texture == nullptr) {
    return;
  }
  // Sometimes textureProxy->isAlphaOnly() != texture->isAlphaOnly(), we use
  // textureProxy->isAlphaOnly() to determine the alpha-only flag.
  bytesKey->write(textureProxy->isAlphaOnly());
  Sampling sampling(texture, samplerState, subset);
  auto flags = static_cast<uint32_t>(sampling.shaderModeX);
  flags |= static_cast<uint32_t>(sampling.shaderModeY) << 4;
  flags |= constraint == SrcRectConstraint::Strict ? static_cast<uint32_t>(1) << 8 : 0;
  bytesKey->write(flags);
}

size_t TiledTextureEffect::onCountTextureSamplers() const {
  auto texture = getTexture();
  return texture ? 1 : 0;
}

const TextureSampler* TiledTextureEffect::onTextureSampler(size_t) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  return texture->getSampler();
}

SamplerState TiledTextureEffect::onSamplerState(size_t) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return {};
  }
  Sampling sampling(texture, samplerState, subset);
  return sampling.hwSampler;
}

const Texture* TiledTextureEffect::getTexture() const {
  auto texture = textureProxy->getTexture().get();
  if (texture && !texture->isYUV()) {
    return texture;
  }
  return nullptr;
}
}  // namespace tgfx
