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
#include <algorithm>
#include "ConstColorProcessor.h"
#include "TextureEffect.h"
#include "core/utils/MathExtra.h"
#include "gpu/ProxyProvider.h"
#include "gpu/SamplerState.h"
#include "gpu/ShaderCaps.h"
#include "gpu/Swizzle.h"
#include "gpu/processors/TextureEffect.h"

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
  return ShaderMode::None;
}

bool TiledTextureEffect::ShaderModeRequiresUnormCoord(ShaderMode mode) {
  switch (mode) {
    case ShaderMode::None:
    case ShaderMode::Clamp:
    case ShaderMode::RepeatNearestNone:
    case ShaderMode::MirrorRepeat:
      return false;
    case ShaderMode::RepeatLinearNone:
    case ShaderMode::RepeatNearestMipmap:
    case ShaderMode::RepeatLinearMipmap:
    case ShaderMode::ClampToBorderNearest:
    case ShaderMode::ClampToBorderLinear:
      return true;
  }
  return false;
}

TiledTextureEffect::Sampling::Sampling(const SamplingSource& source, SamplerState sampler,
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
  auto canDoWrapInHW = [&](TileMode tileMode) {
    if (tileMode == TileMode::Decal && !source.features->clampToBorder) {
      return false;
    }
    if (source.type != TextureType::TwoD &&
        !(tileMode == TileMode::Clamp || tileMode == TileMode::Decal)) {
      return false;
    }
    return true;
  };
  auto resolve = [&](int size, TileMode tileMode, Span subsetSpan, float linearFilterInset) {
    Result1D r;
    // Populate shaderSubset even when the wrap is done in hardware: the shared AOT fill kernels
    // clamp every sample to the Subset uniform unconditionally, so it must hold real bounds (the
    // full texture span here) rather than the empty default. The JIT path ignores it (no Subset
    // uniform is emitted for hardware-wrapped modes).
    r.shaderSubset = subsetSpan;
    bool canDoModeInHW = canDoWrapInHW(tileMode);
    if (canDoModeInHW && subsetSpan.a <= 0 && subsetSpan.b >= static_cast<float>(size)) {
      r.hwMode = tileMode;
      return r;
    }
    if (sampler.magFilterMode == FilterMode::Nearest &&
        sampler.minFilterMode == FilterMode::Nearest) {
      Span isubset{std::floor(subsetSpan.a), std::ceil(subsetSpan.b)};
      // This inset prevents sampling neighboring texels that could occur when
      // texture coords fall exactly at texel boundaries (depending on precision
      // and GPU-specific snapping at the boundary).
      r.shaderClamp = isubset.makeInset(0.5f);
    } else {
      r.shaderClamp = subsetSpan.makeInset(linearFilterInset);
    }
    auto mipmapMode = source.hasMipmaps ? sampler.mipmapMode : MipmapMode::None;
    auto filterMode =
        sampler.minFilterMode == FilterMode::Nearest || sampler.magFilterMode == FilterMode::Nearest
            ? FilterMode::Nearest
            : FilterMode::Linear;
    r.shaderMode = GetShaderMode(tileMode, filterMode, mipmapMode);
    DEBUG_ASSERT(r.shaderMode != ShaderMode::None);
    return r;
  };

  Span subsetX{subset.left, subset.right};
  auto x = resolve(source.width, sampler.tileModeX, subsetX, 0.5f);
  Span subsetY{subset.top, subset.bottom};
  auto y = resolve(source.height, sampler.tileModeY, subsetY, 0.5f);
  hardwareSampler = SamplerState(x.hwMode, y.hwMode, sampler.minFilterMode, sampler.magFilterMode,
                                 sampler.mipmapMode);
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

// TextureView::getTextureCoord for a single-plane view: normalized for 2D textures, unnormalized
// for rectangle textures.
static Point SourceTextureCoord(const TiledTextureEffect::SamplingSource& source, float x,
                                float y) {
  if (source.type == TextureType::Rectangle) {
    return {x, y};
  }
  return {x / static_cast<float>(source.width), y / static_cast<float>(source.height)};
}

static Rect ResolveShaderRect(const TiledTextureEffect::SamplingSource& source, Rect rect,
                              bool usesShaderDimensions) {
  if (source.origin == ImageOrigin::BottomLeft) {
    auto height = static_cast<float>(source.height);
    rect.top = height - rect.top;
    rect.bottom = height - rect.bottom;
    std::swap(rect.top, rect.bottom);
  }
  if (!usesShaderDimensions && source.type != TextureType::Rectangle) {
    auto leftTop = SourceTextureCoord(source, rect.left, rect.top);
    auto rightBottom = SourceTextureCoord(source, rect.right, rect.bottom);
    rect = {leftTop.x, leftTop.y, rightBottom.x, rightBottom.y};
  }
  return rect;
}

bool TiledTextureEffect::getSamplingSource(SamplingSource* source) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return false;
  }
  source->width = textureView->width();
  source->height = textureView->height();
  source->type = textureView->getTexture()->type();
  source->hasMipmaps = textureView->hasMipmaps();
  source->origin = textureView->origin();
  source->features = textureView->getContext()->gpu()->features();
  return true;
}

TiledTextureEffect::Sampling::Sampling(const TextureView* textureView, SamplerState sampler,
                                       const Rect& subset)
    : Sampling(SamplingSource{textureView->width(), textureView->height(),
                              textureView->getTexture()->type(), textureView->hasMipmaps(),
                              textureView->origin(), textureView->getContext()->gpu()->features()},
               sampler, subset) {
}

TiledTextureEffect::ResolvedSampling TiledTextureEffect::makeResolvedSampling(
    const SamplingSource& source) const {
  Sampling sampling(source, samplerState, subset);
  ResolvedSampling resolved = {};
  resolved.hardwareSampler = sampling.hardwareSampler;
  resolved.shaderModeX = sampling.shaderModeX;
  resolved.shaderModeY = sampling.shaderModeY;
  resolved.usesShaderDimensions = (ShaderModeRequiresUnormCoord(resolved.shaderModeX) ||
                                   ShaderModeRequiresUnormCoord(resolved.shaderModeY)) &&
                                  source.type != TextureType::Rectangle;
  if (resolved.usesShaderDimensions) {
    resolved.shaderDimensions = SourceTextureCoord(source, 1.f, 1.f);
  }
  resolved.shaderSubset =
      ResolveShaderRect(source, sampling.shaderSubset, resolved.usesShaderDimensions);
  resolved.shaderClamp =
      ResolveShaderRect(source, sampling.shaderClamp, resolved.usesShaderDimensions);
  resolved.strict = constraint == SrcRectConstraint::Strict;
  resolved.coordMatrix = coordTransform.matrix;
  resolved.hasPerspective = coordTransform.matrix.hasPerspective();
  resolved.alphaOnly = textureProxy->isAlphaOnly();
  resolved.textureOrigin = source.origin;
  return resolved;
}

const TiledTextureEffect::ResolvedSampling* TiledTextureEffect::resolveSampling() const {
  if (resolvedSampling.has_value()) {
    return &*resolvedSampling;
  }
  SamplingSource source = {};
  if (!getSamplingSource(&source)) {
    return nullptr;
  }
  resolvedSampling = makeResolvedSampling(source);
  return &*resolvedSampling;
}

bool TiledTextureEffect::isAlphaOnly() const {
  return textureProxy->isAlphaOnly();
}



size_t TiledTextureEffect::onCountTextureSamplers() const {
  auto textureView = getTextureView();
  return textureView ? 1 : 0;
}

std::shared_ptr<Texture> TiledTextureEffect::onTextureAt(size_t) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  return textureView->getTexture();
}

SamplerState TiledTextureEffect::onSamplerStateAt(size_t) const {
  // Main-form linkage: compute from the live view on every call, no caching — the cached
  // resolveSampling() deferred sampler decisions to first-view timing, which main never does.
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return {};
  }
  SamplingSource source = {};
  if (!getSamplingSource(&source)) {
    return {};
  }
  return makeResolvedSampling(source).hardwareSampler;
}

const TextureView* TiledTextureEffect::getTextureView() const {
  auto textureView = textureProxy->getTextureView().get();
  if (textureView && !textureView->isYUV()) {
    return textureView;
  }
  return nullptr;
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
  flags |= coordTransform.matrix.hasPerspective() ? static_cast<uint32_t>(1) << 9 : 0;
  bytesKey->write(flags);
}

}  // namespace tgfx
