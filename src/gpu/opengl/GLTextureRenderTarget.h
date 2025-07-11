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

#include "gpu/DefaultTexture.h"
#include "gpu/opengl/GLRenderTarget.h"

namespace tgfx {
class GLTextureRenderTarget : public DefaultTexture, public GLRenderTarget {
 public:
  Context* getContext() const override {
    return context;
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  int sampleCount() const override {
    return _sampleCount;
  }

  PixelFormat format() const override {
    return _sampler->format();
  }

  std::shared_ptr<Texture> asTexture() const override {
    return std::static_pointer_cast<Texture>(reference);
  }

  std::shared_ptr<RenderTarget> asRenderTarget() const override {
    return std::static_pointer_cast<GLTextureRenderTarget>(reference);
  }

  unsigned readFrameBufferID() const override {
    return _readFrameBufferID;
  }

  unsigned drawFrameBufferID() const override {
    return _drawFrameBufferID;
  }

 protected:
  void onReleaseGPU() override;

 private:
  int _sampleCount = 1;
  unsigned _readFrameBufferID = 0;
  unsigned _drawFrameBufferID = 0;
  unsigned renderBufferID = 0;

  static std::shared_ptr<RenderTarget> MakeFrom(Context* context,
                                                std::unique_ptr<TextureSampler> sampler, int width,
                                                int height, int sampleCount,
                                                ImageOrigin origin = ImageOrigin::TopLeft,
                                                const ScratchKey& scratchKey = {});

  GLTextureRenderTarget(std::unique_ptr<TextureSampler> sampler, int width, int height,
                        ImageOrigin origin, int sampleCount)
      : DefaultTexture(std::move(sampler), width, height, origin), _sampleCount(sampleCount) {
  }

  friend class RenderTarget;
};
}  // namespace tgfx
