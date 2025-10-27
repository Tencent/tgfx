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

#include "CoordTransform.h"
#include "gpu/Texture.h"

namespace tgfx {
Matrix CoordTransform::getTotalMatrix() const {
  if (textureProxy == nullptr || textureProxy->getTextureView() == nullptr) {
    return matrix;
  }
  auto textureView = textureProxy->getTextureView();
  auto combined = matrix;
  // normalize
  auto scale = textureView->getTextureCoord(1, 1);
  combined.postScale(scale.x, scale.y);
  if (textureView->origin() == ImageOrigin::BottomLeft) {
    combined.postScale(1, -1);
    auto translate = textureView->getTextureCoord(0, static_cast<float>(textureView->height()));
    combined.postTranslate(translate.x, translate.y);
  }
  return combined;
}
}  // namespace tgfx
