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

#include "GLSLTextureEffect.h"
#include <unordered_map>
#include "gpu/YUVColorConversion.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> TextureEffect::MakeRGBAAA(BlockAllocator* allocator,
                                                          std::shared_ptr<TextureProxy> proxy,
                                                          const SamplingArgs& args,
                                                          const Point& alphaStart,
                                                          const Matrix* uvMatrix) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  SamplerState samplerState(args.tileModeX, args.tileModeY, args.sampling);
  return allocator->make<GLSLTextureEffect>(std::move(proxy), alphaStart, samplerState,
                                            args.constraint, matrix, args.sampleArea);
}

GLSLTextureEffect::GLSLTextureEffect(std::shared_ptr<TextureProxy> proxy, const Point& alphaStart,
                                     const SamplerState& samplerState, SrcRectConstraint constraint,
                                     const Matrix& uvMatrix, const std::optional<Rect>& subset)
    : TextureEffect(std::move(proxy), samplerState, constraint, alphaStart, uvMatrix, subset) {
}

void GLSLTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                  UniformData* fragmentUniformData) const {
  auto textureView = getTextureView();
  if (textureView == nullptr || fragmentUniformData == nullptr) {
    return;
  }
  if (alphaStart != Point::Zero()) {
    auto alphaStartValue = textureView->getTextureCoord(alphaStart.x, alphaStart.y);
    fragmentUniformData->setData("AlphaStart", alphaStartValue);
  }
  auto yuvTexture = getYUVTexture();
  if (yuvTexture) {
    if (auto conversion = YUVColorConversionMatrix(yuvTexture->yuvColorSpace())) {
      fragmentUniformData->setData("Mat3ColorConversion", *conversion);
    }
  }
  // Set the Subset uniform when the runtime-generated shader declares it, which happens exactly
  // when needSubset() holds; the hasField gate mirrors that decision instead of re-deriving it.
  if (fragmentUniformData->hasField("Subset")) {
    float rect[4];
    computeSubsetRect(rect);
    fragmentUniformData->setData("Subset", rect);
  }
}

}  // namespace tgfx
