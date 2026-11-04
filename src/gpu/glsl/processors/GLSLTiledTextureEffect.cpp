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
#include "gpu/ShaderCaps.h"
#include "gpu/Swizzle.h"
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

void GLSLTiledTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                       UniformData* fragmentUniformData) const {
  // A fully hardware-tiled sampling (ShaderMode None on both axes) declares no fragment uniforms
  // of its own, so a program whose remaining processors also declare none hands over a null block.
  if (fragmentUniformData == nullptr) {
    return;
  }
  auto sampling = resolveSampling();
  if (sampling == nullptr) {
    return;
  }
  if (fragmentUniformData->hasField("ReadSwizzle")) {
    auto view = getTextureView();
    if (view != nullptr) {
      fragmentUniformData->setData("ReadSwizzle", AOTReadSwizzleMode(view->getTexture()->format()));
    }
  }
  if (sampling->usesShaderDimensions) {
    fragmentUniformData->setData("Dimension", sampling->shaderDimensions);
  }
  if (fragmentUniformData->hasField("Subset")) {
    float rect[4] = {sampling->shaderSubset.left, sampling->shaderSubset.top,
                     sampling->shaderSubset.right, sampling->shaderSubset.bottom};
    fragmentUniformData->setData("Subset", rect);
  }
  if (fragmentUniformData->hasField("Clamp")) {
    float rect[4] = {sampling->shaderClamp.left, sampling->shaderClamp.top,
                     sampling->shaderClamp.right, sampling->shaderClamp.bottom};
    fragmentUniformData->setData("Clamp", rect);
  }
  if (fragmentUniformData->hasField("ShaderModeX")) {
    int modeX = static_cast<int>(sampling->shaderModeX);
    fragmentUniformData->setData("ShaderModeX", modeX);
  }
  if (fragmentUniformData->hasField("ShaderModeY")) {
    int modeY = static_cast<int>(sampling->shaderModeY);
    fragmentUniformData->setData("ShaderModeY", modeY);
  }
  if (fragmentUniformData->hasField("AlphaOnly")) {
    int alphaOnly = sampling->alphaOnly ? 1 : 0;
    fragmentUniformData->setData("AlphaOnly", alphaOnly);
  }
  if (fragmentUniformData->hasField("Strict")) {
    int strict = sampling->strict ? 1 : 0;
    fragmentUniformData->setData("Strict", strict);
  }
}
}  // namespace tgfx
