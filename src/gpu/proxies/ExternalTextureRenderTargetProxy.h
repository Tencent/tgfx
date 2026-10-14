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

#include "gpu/proxies/TextureRenderTargetProxy.h"

namespace tgfx {
class ExternalTextureRenderTargetProxy : public TextureRenderTargetProxy {
 public:
  /**
   * False when the wrapped texture is not a regular 2D texture (a desktop GL rectangle texture,
   * for example). The precompiled kernels only declare 2D samplers, so a dst-reading blend copies
   * the target's pixels into a 2D texture first (OpsCompositor::makeDstTextureInfo). Snapshots
   * copy anyway, because the caller keeps owning the wrapped texture (Surface::makeImageSnapshot).
   */
  bool canBeSampledDirectly() const override {
    return sampledDirectly;
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context) const override;

 private:
  BackendTexture backendTexture = {};
  bool sampledDirectly = true;

  ExternalTextureRenderTargetProxy(const BackendTexture& backendTexture, PixelFormat format,
                                   int sampleCount, ImageOrigin origin = ImageOrigin::TopLeft,
                                   bool adopted = false);
  friend class ProxyProvider;
};
}  // namespace tgfx
