/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ResourceImage.h"
#include "images/RGBAAAImage.h"

namespace tgfx {

ResourceImage::ResourceImage(ResourceKey resourceKey) : resourceKey(std::move(resourceKey)) {
}

std::shared_ptr<Image> ResourceImage::onMakeRGBAAA(int displayWidth, int displayHeight,
                                                   int alphaStartX, int alphaStartY) const {
  if (isAlphaOnly()) {
    return nullptr;
  }
  return RGBAAAImage::MakeFrom(weakThis.lock(), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}
}  // namespace tgfx
