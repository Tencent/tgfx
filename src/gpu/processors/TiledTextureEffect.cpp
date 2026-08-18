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
#include "gpu/AOTEffect.h"
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
  auto features = textureView->getContext()->gpu()->features();
  auto canDoWrapInHW = [&](TileMode tileMode) {
    if (tileMode == TileMode::Decal && !features->clampToBorder) {
      return false;
    }
    if (textureView->getTexture()->type() != TextureType::TwoD &&
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
    auto mipmapMode = textureView->hasMipmaps() ? sampler.mipmapMode : MipmapMode::None;
    auto filterMode =
        sampler.minFilterMode == FilterMode::Nearest || sampler.magFilterMode == FilterMode::Nearest
            ? FilterMode::Nearest
            : FilterMode::Linear;
    r.shaderMode = GetShaderMode(tileMode, filterMode, mipmapMode);
    DEBUG_ASSERT(r.shaderMode != ShaderMode::None);
    return r;
  };

  Span subsetX{subset.left, subset.right};
  auto x = resolve(textureView->width(), sampler.tileModeX, subsetX, 0.5f);
  Span subsetY{subset.top, subset.bottom};
  auto y = resolve(textureView->height(), sampler.tileModeY, subsetY, 0.5f);
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

static Rect ResolveShaderRect(const TextureView* textureView, Rect rect,
                              bool usesShaderDimensions) {
  if (textureView->origin() == ImageOrigin::BottomLeft) {
    auto height = static_cast<float>(textureView->height());
    rect.top = height - rect.top;
    rect.bottom = height - rect.bottom;
    std::swap(rect.top, rect.bottom);
  }
  if (!usesShaderDimensions && textureView->getTexture()->type() != TextureType::Rectangle) {
    auto leftTop = textureView->getTextureCoord(rect.left, rect.top);
    auto rightBottom = textureView->getTextureCoord(rect.right, rect.bottom);
    rect = {leftTop.x, leftTop.y, rightBottom.x, rightBottom.y};
  }
  return rect;
}

const TiledTextureEffect::ResolvedSampling* TiledTextureEffect::resolveSampling() const {
  if (resolvedSampling.has_value()) {
    return &*resolvedSampling;
  }
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  Sampling sampling(textureView, samplerState, subset);
  ResolvedSampling resolved = {};
  resolved.hardwareSampler = sampling.hardwareSampler;
  resolved.shaderModeX = sampling.shaderModeX;
  resolved.shaderModeY = sampling.shaderModeY;
  resolved.usesShaderDimensions = (ShaderModeRequiresUnormCoord(resolved.shaderModeX) ||
                                   ShaderModeRequiresUnormCoord(resolved.shaderModeY)) &&
                                  textureView->getTexture()->type() != TextureType::Rectangle;
  if (resolved.usesShaderDimensions) {
    resolved.shaderDimensions = textureView->getTextureCoord(1.f, 1.f);
  }
  resolved.shaderSubset =
      ResolveShaderRect(textureView, sampling.shaderSubset, resolved.usesShaderDimensions);
  resolved.shaderClamp =
      ResolveShaderRect(textureView, sampling.shaderClamp, resolved.usesShaderDimensions);
  resolved.strict = constraint == SrcRectConstraint::Strict;
  resolved.coordMatrix = coordTransform.matrix;
  resolved.hasPerspective = coordTransform.matrix.hasPerspective();
  resolved.alphaOnly = textureProxy->isAlphaOnly();
  resolved.textureOrigin = textureView->origin();
  resolvedSampling = resolved;
  return &*resolvedSampling;
}

void TiledTextureEffect::getShaderModes(int* outModeX, int* outModeY) const {
  auto sampling = resolveSampling();
  if (sampling == nullptr) {
    *outModeX = 0;
    *outModeY = 0;
    return;
  }
  *outModeX = static_cast<int>(sampling->shaderModeX);
  *outModeY = static_cast<int>(sampling->shaderModeY);
}

bool TiledTextureEffect::isAlphaOnly() const {
  return textureProxy->isAlphaOnly();
}

bool TiledTextureEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                    AOTNodeID* output) const {
  auto textureView = getTextureView();
  if (builder == nullptr || output == nullptr || textureView == nullptr) {
    return false;
  }
  // Rectangle textures are accepted: the precompiled chain kernel carries a TEXTURE_KIND dimension
  // (sampler2DRect variants), and its shared tiled-sampling path is rectangle-aware
  // (tiledEffectiveUnorm keeps rectangle coordinates in pixel space). External textures still
  // cannot be served. Restriction: only fully hardware-resolved wraps (shader mode None) ride the
  // rectangle path for now — shader-emulated modes (repeat/mirror/decal) inside a folded chain
  // render divergently on rectangle textures and stay on the runtime route until wired.
  auto textureType = textureView->getTexture()->type();
  if (textureType == TextureType::Rectangle) {
    auto resolvedCheck = resolveSampling();
    if (resolvedCheck == nullptr || resolvedCheck->shaderModeX != TiledTextureShaderMode::None ||
        resolvedCheck->shaderModeY != TiledTextureShaderMode::None) {
      return false;
    }
  }
  if (textureType != TextureType::TwoD && textureType != TextureType::Rectangle) {
    return false;
  }
  auto resolved = resolveSampling();
  if (resolved == nullptr) {
    return false;
  }
  AOTTextureParameters parameters = {};
  parameters.textureProxy = textureProxy;
  parameters.samplingKind = AOTTextureSamplingKind::Tiled;
  parameters.samplerState = resolved->hardwareSampler;
  parameters.constraint = constraint;
  parameters.uvMatrix = resolved->coordMatrix;
  parameters.subset = subset;
  parameters.tiledRecipe = *resolved;
  parameters.isAlphaOnly = resolved->alphaOnly;
  parameters.hasSubset = subset != Rect::MakeWH(textureProxy->width(), textureProxy->height());
  parameters.hasPerspective = resolved->hasPerspective;
  return builder->addTextureSource(input, parameters, output);
}

void TiledTextureEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  auto sampling = resolveSampling();
  if (sampling == nullptr) {
    return;
  }
  bytesKey->write(sampling->alphaOnly);
  auto flags = static_cast<uint32_t>(sampling->shaderModeX);
  flags |= static_cast<uint32_t>(sampling->shaderModeY) << 4;
  flags |= sampling->strict ? static_cast<uint32_t>(1) << 8 : 0;
  flags |= sampling->hasPerspective ? static_cast<uint32_t>(1) << 9 : 0;
  bytesKey->write(flags);
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
  auto sampling = resolveSampling();
  return sampling != nullptr ? sampling->hardwareSampler : SamplerState();
}

const TextureView* TiledTextureEffect::getTextureView() const {
  auto textureView = textureProxy->getTextureView().get();
  if (textureView && !textureView->isYUV()) {
    return textureView;
  }
  return nullptr;
}
}  // namespace tgfx
