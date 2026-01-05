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
#include "gpu/resources/RenderTarget.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/gpu/CommandQueue.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx::inspect {

class FrameCaptureTexture {
 public:
  static std::shared_ptr<FrameCaptureTexture> MakeFrom(std::shared_ptr<Texture> texture, int width,
                                                       int height, size_t rowBytes,
                                                       PixelFormat format, const void* pixels);

  static std::shared_ptr<FrameCaptureTexture> MakeFrom(std::shared_ptr<Texture> texture,
                                                       Context* context);

  static std::shared_ptr<FrameCaptureTexture> MakeFrom(const RenderTarget* renderTarget);

  static uint64_t GetReadTextureId(std::shared_ptr<Texture> texture);

  static void ClearReadedTexture();

  explicit FrameCaptureTexture(std::shared_ptr<Texture> texture, int width, int height,
                               size_t rowBytes, PixelFormat format, bool isInput,
                               std::shared_ptr<Data> pixels);

  uint64_t textureId() const {
    return _textureId;
  }

  std::shared_ptr<Texture> texture() const {
    return _texture;
  }

  bool isInput() const {
    return _isInput;
  }

  PixelFormat format() const {
    return _format;
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  size_t rowBytes() const {
    return _rowBytes;
  }

  std::shared_ptr<Data> imagePixels() const {
    return pixels;
  }

 private:
  uint64_t _textureId = 0;
  std::shared_ptr<Texture> _texture = nullptr;
  int _width = 0;
  int _height = 0;
  size_t _rowBytes = 0;
  PixelFormat _format = PixelFormat::Unknown;
  bool _isInput = false;
  std::shared_ptr<Data> pixels = nullptr;
};

}  // namespace tgfx::inspect
