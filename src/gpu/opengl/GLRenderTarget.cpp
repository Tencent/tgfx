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

#include "GLRenderTarget.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLUtil.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {
static bool CanReadDirectly(Context* context, ImageOrigin origin, const ImageInfo& srcInfo,
                            const ImageInfo& dstInfo) {
  if (origin != ImageOrigin::TopLeft || dstInfo.alphaType() != srcInfo.alphaType() ||
      dstInfo.colorType() != srcInfo.colorType()) {
    return false;
  }
  auto caps = GLCaps::Get(context);
  if (dstInfo.rowBytes() != dstInfo.minRowBytes() && !caps->packRowLengthSupport) {
    return false;
  }
  return true;
}

static void CopyPixels(const ImageInfo& srcInfo, const void* srcPixels, const ImageInfo& dstInfo,
                       void* dstPixels, bool flipY) {
  auto pixels = srcPixels;
  Buffer tempBuffer = {};
  if (flipY) {
    tempBuffer.alloc(srcInfo.byteSize());
    auto rowCount = static_cast<size_t>(srcInfo.height());
    auto rowBytes = srcInfo.rowBytes();
    auto dst = tempBuffer.bytes();
    for (size_t i = 0; i < rowCount; i++) {
      auto src = reinterpret_cast<const uint8_t*>(srcPixels) + (rowCount - i - 1) * rowBytes;
      memcpy(dst, src, rowBytes);
      dst += rowBytes;
    }
    pixels = tempBuffer.data();
  }
  Pixmap pixmap(srcInfo, pixels);
  pixmap.readPixels(dstInfo, dstPixels);
}

BackendRenderTarget GLRenderTarget::getBackendRenderTarget() const {
  GLFrameBufferInfo glInfo = {};
  glInfo.id = drawFrameBufferID();
  glInfo.format = PixelFormatToGLSizeFormat(format());
  return {glInfo, width(), height()};
}

bool GLRenderTarget::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX,
                                int srcY) const {
  dstPixels = dstInfo.computeOffset(dstPixels, -srcX, -srcY);
  auto outInfo = dstInfo.makeIntersect(-srcX, -srcY, width(), height());
  if (outInfo.isEmpty()) {
    return false;
  }
  auto context = getContext();
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  const auto& textureFormat = caps->getTextureFormat(format());
  gl->bindFramebuffer(GL_FRAMEBUFFER, readFrameBufferID());

  auto colorType = PixelFormatToColorType(format());
  auto srcInfo =
      ImageInfo::Make(outInfo.width(), outInfo.height(), colorType, AlphaType::Premultiplied);
  void* pixels = nullptr;
  Buffer tempBuffer = {};
  auto restoreGLRowLength = false;
  if (CanReadDirectly(context, origin(), srcInfo, outInfo)) {
    pixels = dstPixels;
    if (outInfo.rowBytes() != outInfo.minRowBytes()) {
      gl->pixelStorei(GL_PACK_ROW_LENGTH,
                      static_cast<int>(outInfo.rowBytes() / outInfo.bytesPerPixel()));
      restoreGLRowLength = true;
    }
  } else {
    tempBuffer.alloc(srcInfo.byteSize());
    pixels = tempBuffer.data();
  }
  auto alignment = format() == PixelFormat::ALPHA_8 ? 1 : 4;
  gl->pixelStorei(GL_PACK_ALIGNMENT, alignment);
  auto flipY = origin() == ImageOrigin::BottomLeft;
  auto readX = std::max(0, srcX);
  auto readY = std::max(0, srcY);
  if (flipY) {
    readY = height() - readY - outInfo.height();
  }
  gl->readPixels(readX, readY, outInfo.width(), outInfo.height(), textureFormat.externalFormat,
                 GL_UNSIGNED_BYTE, pixels);
  if (restoreGLRowLength) {
    gl->pixelStorei(GL_PACK_ROW_LENGTH, 0);
  }
  if (!tempBuffer.isEmpty()) {
    CopyPixels(srcInfo, tempBuffer.data(), outInfo, dstPixels, flipY);
  }
  return true;
}

}  // namespace tgfx
