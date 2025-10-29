/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "HardwareTextureProxy.h"

namespace tgfx {
HardwareTextureProxy::HardwareTextureProxy(HardwareBufferRef hardwareBuffer, int width, int height,
                                           PixelFormat format)
    : TextureProxy(width, height, format), hardwareBuffer(hardwareBuffer) {
  HardwareBufferRetain(hardwareBuffer);
}

HardwareTextureProxy::~HardwareTextureProxy() {
  HardwareBufferRelease(hardwareBuffer);
}

std::shared_ptr<TextureView> HardwareTextureProxy::getTextureView() const {
  if (resource == nullptr) {
    resource = TextureView::MakeFrom(context, hardwareBuffer);
    if (resource != nullptr && !uniqueKey.empty()) {
      resource->assignUniqueKey(uniqueKey);
    }
  }
  return std::static_pointer_cast<TextureView>(resource);
}
}  // namespace tgfx