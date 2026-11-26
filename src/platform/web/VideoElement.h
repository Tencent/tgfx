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

#pragma once

#include <emscripten/val.h>
#include <cstdint>
#include "platform/ImageStream.h"

namespace tgfx {
/**
 * The VideoElement class allows direct access to image buffers rendered into a HTMLVideoElement
 * on the web platform. It is typically used with the ImageReader class.
 */
class VideoElement : public ImageStream {
 public:
  /**
   * Creates a new VideoElement from the specified HTMLVideoElement object and the video size.
   * Returns nullptr if the video is null or either dimension is non-positive.
   */
  static std::shared_ptr<VideoElement> MakeFrom(emscripten::val video, int width, int height);

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return ColorSpace::SRGB();
  }

 protected:
  TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                  std::shared_ptr<TextureView> texture) override;

 private:
  emscripten::val source = emscripten::val::null();
#ifdef TGFX_USE_WEBGPU
  // Caches the WGPUDevice handle of the context that created the texture, so that the JS side can
  // resolve the same GPUDevice object via WebGPU.mgrDevice when uploading video frames.
  uintptr_t _deviceHandle = 0;
#endif

  VideoElement(emscripten::val video, int width, int height);

  std::shared_ptr<TextureView> makeTexture(Context* context, bool mipmapped);

  void uploadTexture(const std::shared_ptr<TextureView>& textureView);
};
}  // namespace tgfx
