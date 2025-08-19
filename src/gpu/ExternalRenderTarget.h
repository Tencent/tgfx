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

#include "gpu/RenderTarget.h"
#include "gpu/Resource.h"

namespace tgfx {
class ExternalRenderTarget : public Resource, public RenderTarget {
 public:
  Context* getContext() const override {
    return context;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  bool externallyOwned() const override {
    return true;
  }

  GPUTexture* getRenderTexture() const override {
    return renderTexture.get();
  }

  GPUTexture* getSampleTexture() const override {
    return renderTexture.get();
  }

  size_t memoryUsage() const override {
    return 0;
  }

 protected:
  void onReleaseGPU() override {
    renderTexture->release(context->gpu());
  }

 private:
  std::unique_ptr<GPUTexture> renderTexture = nullptr;
  ImageOrigin _origin = ImageOrigin::TopLeft;

  ExternalRenderTarget(std::unique_ptr<GPUTexture> texture, ImageOrigin origin)
      : renderTexture(std::move(texture)), _origin(origin) {
  }

  friend class RenderTarget;
};
}  // namespace tgfx
