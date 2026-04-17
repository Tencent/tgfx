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

#include "GLSLTiledTextureEffect.h"
#include "gpu/SamplerState.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> TiledTextureEffect::Make(BlockAllocator* allocator,
                                                         std::shared_ptr<TextureProxy> proxy,
                                                         const SamplingArgs& args,
                                                         const Matrix* uvMatrix, bool forceAsMask) {
  if (proxy == nullptr) {
    return nullptr;
  }
  if (args.tileModeX == TileMode::Clamp && args.tileModeY == TileMode::Clamp) {
    return TextureEffect::Make(allocator, std::move(proxy), args, uvMatrix, forceAsMask);
  }
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  SamplerState samplerState(args.tileModeX, args.tileModeY, args.sampling);
  auto isAlphaOnly = proxy->isAlphaOnly();
  PlacementPtr<FragmentProcessor> processor = allocator->make<GLSLTiledTextureEffect>(
      std::move(proxy), samplerState, args.constraint, matrix, args.sampleArea);
  if (forceAsMask && !isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(allocator, std::move(processor));
  }
  return processor;
}

GLSLTiledTextureEffect::GLSLTiledTextureEffect(std::shared_ptr<TextureProxy> proxy,
                                               const SamplerState& samplerState,
                                               SrcRectConstraint constraint, const Matrix& uvMatrix,
                                               const std::optional<Rect>& subset)
    : TiledTextureEffect(std::move(proxy), samplerState, constraint, uvMatrix, subset) {
}

bool GLSLTiledTextureEffect::ShaderModeRequiresUnormCoord(TiledTextureEffect::ShaderMode mode) {
  switch (mode) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
      return false;
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return true;
  }
  return false;
}

bool GLSLTiledTextureEffect::ShaderModeUsesSubset(TiledTextureEffect::ShaderMode m) {
  switch (m) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return false;
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
      return true;
  }
  return false;
}

bool GLSLTiledTextureEffect::ShaderModeUsesClamp(TiledTextureEffect::ShaderMode m) {
  switch (m) {
    case TiledTextureEffect::ShaderMode::None:
    case TiledTextureEffect::ShaderMode::ClampToBorderNearest:
      return false;
    case TiledTextureEffect::ShaderMode::Clamp:
    case TiledTextureEffect::ShaderMode::RepeatNearestNone:
    case TiledTextureEffect::ShaderMode::RepeatLinearNone:
    case TiledTextureEffect::ShaderMode::RepeatNearestMipmap:
    case TiledTextureEffect::ShaderMode::RepeatLinearMipmap:
    case TiledTextureEffect::ShaderMode::MirrorRepeat:
    case TiledTextureEffect::ShaderMode::ClampToBorderLinear:
      return true;
  }
  return false;
}

void GLSLTiledTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                       UniformData* fragmentUniformData) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return;
  }
  Sampling sampling(textureView, samplerState, subset);
  auto hasDimensionUniform = (ShaderModeRequiresUnormCoord(sampling.shaderModeX) ||
                              ShaderModeRequiresUnormCoord(sampling.shaderModeY)) &&
                             textureView->getTexture()->type() != TextureType::Rectangle;
  if (hasDimensionUniform) {
    auto dimensions = textureView->getTextureCoord(1.f, 1.f);
    fragmentUniformData->setData("Dimension", dimensions);
  }
  auto pushRect = [&](Rect subset, const std::string& uni) {
    float rect[4] = {subset.left, subset.top, subset.right, subset.bottom};
    if (textureView->origin() == ImageOrigin::BottomLeft) {
      auto h = static_cast<float>(textureView->height());
      rect[1] = h - rect[1];
      rect[3] = h - rect[3];
      std::swap(rect[1], rect[3]);
    }
    auto type = textureView->getTexture()->type();
    if (!hasDimensionUniform && type != TextureType::Rectangle) {
      auto lt =
          textureView->getTextureCoord(static_cast<float>(rect[0]), static_cast<float>(rect[1]));
      auto rb =
          textureView->getTextureCoord(static_cast<float>(rect[2]), static_cast<float>(rect[3]));
      rect[0] = lt.x;
      rect[1] = lt.y;
      rect[2] = rb.x;
      rect[3] = rb.y;
    }
    fragmentUniformData->setData(uni, rect);
  };
  if (ShaderModeUsesSubset(sampling.shaderModeX) || ShaderModeUsesSubset(sampling.shaderModeY)) {
    pushRect(sampling.shaderSubset, "Subset");
  }
  if (ShaderModeUsesClamp(sampling.shaderModeX) || ShaderModeUsesClamp(sampling.shaderModeY)) {
    pushRect(sampling.shaderClamp, "Clamp");
  }
}
}  // namespace tgfx
