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

#include "gpu/DefaultTextureView.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
class TextureRenderTarget : public DefaultTextureView, public RenderTarget {
 public:
  Context* getContext() const override {
    return context;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  bool externallyOwned() const override {
    return _externallyOwned;
  }

  GPUTexture* getRenderTexture() const override {
    return renderTexture ? renderTexture.get() : _texture.get();
  }

  GPUTexture* getSampleTexture() const override {
    return _texture.get();
  }

  std::shared_ptr<TextureView> asTextureView() const override {
    return std::static_pointer_cast<TextureView>(reference);
  }

  std::shared_ptr<RenderTarget> asRenderTarget() const override {
    return std::static_pointer_cast<TextureRenderTarget>(reference);
  }

  void onReleaseGPU() override;

 private:
  std::unique_ptr<GPUTexture> renderTexture = nullptr;
  bool _externallyOwned = false;

  static std::shared_ptr<RenderTarget> MakeFrom(Context* context,
                                                std::unique_ptr<GPUTexture> texture,
                                                int sampleCount,
                                                ImageOrigin origin = ImageOrigin::TopLeft,
                                                bool externallyOwned = false,
                                                const ScratchKey& scratchKey = {});

  TextureRenderTarget(std::unique_ptr<GPUTexture> texture,
                      std::unique_ptr<GPUTexture> renderTexture, ImageOrigin origin,
                      bool externallyOwned)
      : DefaultTextureView(std::move(texture), origin), renderTexture(std::move(renderTexture)),
        _externallyOwned(externallyOwned) {
  }

  friend class RenderTarget;
};
}  // namespace tgfx
