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
#include "core/utils/PixelFormatUtil.h"
#include "gpu/GPU.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/opengl/GLGPU.h"

namespace tgfx::inspect {

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(GPUTexture* texture, int width,
                                                                   int height, size_t rowBytes,
                                                                   PixelFormat format,
                                                                   const void* pixels) {
  const auto sz = static_cast<size_t>(height) * rowBytes;
  auto imageBuffer = std::make_shared<Buffer>(sz);
  imageBuffer->writeRange(0, sz, pixels);
  return std::make_shared<FrameCaptureTexture>(texture, width, height, rowBytes, format, true,
                                               std::move(imageBuffer));
}

std::shared_ptr<FrameCaptureTexture> FrameCaptureTexture::MakeFrom(GPUTexture* texture,
                                                                   Context* context) {
  auto width = texture->width();
  auto height = texture->height();
  auto format = texture->format();
  auto commandQueue = context->gpu()->queue();
  auto colorType = PixelFormatToColorType(format);
  auto rowBytes = ImageInfo::GetBytesPerPixel(colorType) * static_cast<size_t>(width);
  auto buffer = std::make_shared<Buffer>(rowBytes * static_cast<size_t>(height));
  if (!commandQueue->readTexture(texture, Rect::MakeWH(width, height), buffer->bytes(),
                                 rowBytes)) {
    return nullptr;
  }
  return std::make_shared<FrameCaptureTexture>(texture, width, height, rowBytes,
                                               texture->format(), true, buffer);
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
}  // namespace tgfx::inspect