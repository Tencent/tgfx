/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AtlasTextureEffect.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> AtlasTextureEffect::Make(std::shared_ptr<TextureProxy> proxy,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix, bool forceAsMask) {

  if (proxy == nullptr || !proxy->isAlphaOnly()) {
    return nullptr;
  }
  auto isAlphaOnly = proxy->isAlphaOnly();
  auto matrix = uvMatrix ? *uvMatrix : Matrix::I();
  auto drawingBuffer = proxy->getContext()->drawingBuffer();
  auto processor =
      drawingBuffer->make<GLAtlasTextureEffect>(std::move(proxy), sampling, matrix);
  return processor;
}

}  // namespace tgfx
