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

#include "gpu/resources/DefaultTextureView.h"
#include "gpu/resources/RenderTarget.h"

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

  std::shared_ptr<Texture> getRenderTexture() const override {
    return renderTexture ? renderTexture : _texture;
  }

  std::shared_ptr<Texture> getSampleTexture() const override {
    return _texture;
  }

  std::shared_ptr<TextureView> asTextureView() const override {
    return std::static_pointer_cast<TextureView>(weakThis.lock());
  }

  std::shared_ptr<RenderTarget> asRenderTarget() const override {
    return std::static_pointer_cast<TextureRenderTarget>(weakThis.lock());
  }

 private:
  std::shared_ptr<Texture> renderTexture = nullptr;
  bool _externallyOwned = false;

  static std::shared_ptr<RenderTarget> MakeFrom(Context* context, std::shared_ptr<Texture> texture,
                                                int sampleCount,
                                                ImageOrigin origin = ImageOrigin::TopLeft,
                                                bool externallyOwned = false,
                                                const ScratchKey& scratchKey = {});

  TextureRenderTarget(std::shared_ptr<Texture> texture, std::shared_ptr<Texture> renderTexture,
                      ImageOrigin origin, bool externallyOwned)
      : DefaultTextureView(std::move(texture), origin), renderTexture(std::move(renderTexture)),
        _externallyOwned(externallyOwned) {
  }

  friend class RenderTarget;
};
}  // namespace tgfx
