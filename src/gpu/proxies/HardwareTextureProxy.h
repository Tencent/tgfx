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

#pragma once

#include "gpu/proxies/TextureProxy.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
class HardwareTextureProxy final : public TextureProxy {
 public:
  std::shared_ptr<TextureView> getTextureView() const override;

  ~HardwareTextureProxy() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

 private:
  HardwareTextureProxy(HardwareBufferRef hardwareBuffer, int width, int height, PixelFormat format);

  HardwareBufferRef hardwareBuffer = nullptr;
  friend class ProxyProvider;
};
}  // namespace tgfx