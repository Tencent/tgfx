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

#include "FrameCaptureTexture.h"
#include <set>
#include "FrameCapture.h"
#include "core/utils/CopyPixels.h"
#include "core/utils/PixelFormatUtil.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx::inspect {
static std::unordered_map<uint64_t, uint64_t> ReadedInputTexture = {};

static std::shared_ptr<Data> ReadTexture(GPU* gpu, std::shared_ptr<Texture> texture,
                                         bool flipY = false) {
  DEBUG_ASSERT(texture != nullptr);
  auto width = texture->width();
  auto height = texture->height();
  auto format = texture->format();
  auto srcInfo = ImageInfo::Make(width, height, PixelFormatToColorType(format));
  auto readbackBuffer = gpu->createBuffer(srcInfo.byteSize(), GPUBufferUsage::READBACK);
  if (readbackBuffer == nullptr) {
    return nullptr;
  }
  auto encoder = gpu->createCommandEncoder();
  DEBUG_ASSERT(encoder != nullptr);
  auto rect = Rect::MakeXYWH(0, 0, width, height);
  encoder->copyTextureToBuffer(texture, rect, readbackBuffer);
  auto commandBuffer = encoder->finish();
  if (commandBuffer == nullptr) {
    return nullptr;
  }
  gpu->queue()->submit(commandBuffer);
  Buffer buffer(srcInfo.byteSize());
  if (buffer.isEmpty()) {
    return nullptr;
  }
  auto srcPixels = readbackBuffer->map();
  if (srcPixels == nullptr) {
    return nullptr;
  }
  CopyPixels(srcInfo, srcPixels, srcInfo, buffer.bytes(), flipY);
  readbackBuffer->unmap();
  return buffer.release();
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(std::shared_ptr<Texture> texture,
                                                                   int width, int height,
                                                                   size_t rowBytes,
                                                                   PixelFormat format,
                                                                   const void* pixels) {
  const auto size = static_cast<size_t>(height) * rowBytes;
  auto data = Data::MakeWithCopy(pixels, size);
  return std::make_shared<FrameCaptureTexture>(std::move(texture), width, height, rowBytes, format,
                                               true, std::move(data));
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(std::shared_ptr<Texture> texture,
                                                                   Context* context) {
  auto textureKey = reinterpret_cast<uint64_t>(texture.get());
  if (ReadedInputTexture.find(textureKey) != ReadedInputTexture.end()) {
    return nullptr;
  }
  auto pixels = ReadTexture(context->gpu(), texture);
  if (pixels == nullptr) {
    return nullptr;
  }
  auto width = texture->width();
  auto height = texture->height();
  auto format = texture->format();
  auto colorType = PixelFormatToColorType(format);
  auto rowBytes = ImageInfo::GetBytesPerPixel(colorType) * static_cast<size_t>(width);
  auto frameCaptureTexture = std::make_shared<FrameCaptureTexture>(
      texture, width, height, rowBytes, texture->format(), true, std::move(pixels));
  ReadedInputTexture[textureKey] = frameCaptureTexture->textureId();
  return frameCaptureTexture;
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(
    const RenderTarget* renderTarget) {
  auto width = renderTarget->width();
  auto height = renderTarget->height();
  auto format = renderTarget->format();
  auto rowBytes =
      static_cast<size_t>(width) * ImageInfo::GetBytesPerPixel(PixelFormatToColorType(format));
  auto gpu = renderTarget->getContext()->gpu();
  auto flipY = renderTarget->origin() == ImageOrigin::BottomLeft;
  auto pixels = ReadTexture(gpu, renderTarget->getSampleTexture(), flipY);
  return std::make_shared<FrameCaptureTexture>(renderTarget->getRenderTexture(), width, height,
                                               rowBytes, format, false, std::move(pixels));
}

uint64_t FrameCaptureTexture::GetReadTextureId(std::shared_ptr<Texture> texture) {
  auto result = ReadedInputTexture.find(reinterpret_cast<uint64_t>(texture.get()));
  if (result == ReadedInputTexture.end()) {
    return 0;
  }
  return result->second;
}

void FrameCaptureTexture::ClearReadedTexture() {
  ReadedInputTexture.clear();
}

FrameCaptureTexture::FrameCaptureTexture(std::shared_ptr<Texture> texture, int width, int height,
                                         size_t rowBytes, PixelFormat format, bool isInput,
                                         std::shared_ptr<Data> pixels)
    : _textureId(FrameCapture::NextTextureID()), _texture(texture), _width(width), _height(height),
      _rowBytes(rowBytes), _format(format), _isInput(isInput), pixels(std::move(pixels)) {
}
}  // namespace tgfx::inspect
