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

#include <set>
#include "FrameCapture.h"
#include "FrameCaptureTexture.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/GPU.h"

namespace tgfx::inspect {
static std::unordered_map<uint64_t, uint64_t> ReadedInputTexture = {};

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(
    std::shared_ptr<GPUTexture> texture, int width, int height, size_t rowBytes, PixelFormat format,
    const void* pixels) {
  const auto size = static_cast<size_t>(height) * rowBytes;
  auto imageBuffer = std::make_shared<Buffer>(size);
  imageBuffer->writeRange(0, size, pixels);
  return std::make_shared<FrameCaptureTexture>(std::move(texture), width, height, rowBytes, format,
                                               true, std::move(imageBuffer));
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(
    std::shared_ptr<GPUTexture> texture, Context* context) {
  auto textureKey = reinterpret_cast<uint64_t>(texture.get());
  if (ReadedInputTexture.find(textureKey) != ReadedInputTexture.end()) {
    return nullptr;
  }
  auto width = texture->width();
  auto height = texture->height();
  auto format = texture->format();
  auto commandQueue = context->gpu()->queue();
  auto colorType = PixelFormatToColorType(format);
  auto rowBytes = ImageInfo::GetBytesPerPixel(colorType) * static_cast<size_t>(width);
  auto buffer = std::make_shared<Buffer>(rowBytes * static_cast<size_t>(height));
  if (!commandQueue->readTexture(texture, Rect::MakeWH(width, height), buffer->bytes(), rowBytes)) {
    return nullptr;
  }
  auto frameCaptureTexture = std::make_shared<FrameCaptureTexture>(texture, width, height, rowBytes,
                                                                   texture->format(), true, buffer);
  ReadedInputTexture[textureKey] = frameCaptureTexture->textureId();
  return frameCaptureTexture;
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(
    const RenderTarget* renderTarget) {
  auto width = renderTarget->width();
  auto height = renderTarget->height();
  auto format = renderTarget->format();
  auto imageInfo = ImageInfo::Make(width, height, PixelFormatToColorType(format));
  auto imageBuffer = std::make_shared<Buffer>(imageInfo.byteSize());
  if (!renderTarget->readPixels(imageInfo, imageBuffer->bytes())) {
    LOGI("FrameCaptureTexture read renderTarget error");
    return nullptr;
  }
  return std::make_shared<FrameCaptureTexture>(renderTarget->getRenderTexture(), width, height,
                                               imageInfo.rowBytes(), format, false,
                                               std::move(imageBuffer));
}

uint64_t FrameCaptureTexture::GetReadTextureId(std::shared_ptr<GPUTexture> texture) {
  auto result = ReadedInputTexture.find(reinterpret_cast<uint64_t>(texture.get()));
  if (result == ReadedInputTexture.end()) {
    return 0;
  }
  return result->second;
}

void FrameCaptureTexture::ClearReadedTexture() {
  ReadedInputTexture.clear();
}

FrameCaptureTexture::FrameCaptureTexture(const std::shared_ptr<GPUTexture> texture, int width,
                                         int height, size_t rowBytes, PixelFormat format,
                                         bool isInput, std::shared_ptr<Buffer> imageBuffer)
    : _textureId(FrameCapture::NextTextureID()), _texture(texture), _width(width), _height(height),
      _rowBytes(rowBytes), _format(format), _isInput(isInput), image(std::move(imageBuffer)) {
}
}  // namespace tgfx::inspect