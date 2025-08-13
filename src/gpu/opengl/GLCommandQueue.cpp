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

#include "GLCommandQueue.h"
#include "GLTexture.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLFrameBuffer.h"
#include "gpu/opengl/GLUtil.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
bool GLCommandQueue::writeBuffer(GPUBuffer* buffer, size_t bufferOffset, const void* data,
                                 size_t size) {
  if (data == nullptr || size == 0) {
    LOGE("GLCommandQueue::writeBuffer() data is null or size is zero!");
    return false;
  }
  if (bufferOffset + size > buffer->size()) {
    LOGE("GLCommandQueue::writeBuffer() size exceeds buffer size!");
    return false;
  }
  auto gl = interface->functions();
  ClearGLError(gl);
  auto glBuffer = static_cast<const GLBuffer*>(buffer);
  auto target = glBuffer->target();
  gl->bindBuffer(target, glBuffer->bufferID());
  gl->bufferSubData(target, static_cast<GLintptr>(bufferOffset), static_cast<GLsizeiptr>(size),
                    data);
  gl->bindBuffer(target, 0);
  return CheckGLError(gl);
}

void GLCommandQueue::writeTexture(GPUTexture* texture, const Rect& rect, const void* pixels,
                                  size_t rowBytes) {
  if (texture == nullptr || rect.isEmpty()) {
    return;
  }
  auto gl = interface->functions();
  auto caps = interface->caps();
  if (caps->flushBeforeWritePixels) {
    gl->flush();
  }
  auto glTexture = static_cast<const GLTexture*>(texture);
  gl->bindTexture(glTexture->target(), glTexture->id());
  const auto& textureFormat = caps->getTextureFormat(glTexture->format());
  auto bytesPerPixel = PixelFormatBytesPerPixel(glTexture->format());
  gl->pixelStorei(GL_UNPACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  int x = static_cast<int>(rect.x());
  int y = static_cast<int>(rect.y());
  int width = static_cast<int>(rect.width());
  int height = static_cast<int>(rect.height());
  if (caps->unpackRowLengthSupport) {
    // the number of pixels, not bytes
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<int>(rowBytes / bytesPerPixel));
    gl->texSubImage2D(glTexture->target(), 0, x, y, width, height, textureFormat.externalFormat,
                      GL_UNSIGNED_BYTE, pixels);
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  } else {
    if (static_cast<size_t>(width) * bytesPerPixel == rowBytes) {
      gl->texSubImage2D(glTexture->target(), 0, x, y, width, height, textureFormat.externalFormat,
                        GL_UNSIGNED_BYTE, pixels);
    } else {
      auto data = reinterpret_cast<const uint8_t*>(pixels);
      for (int row = 0; row < height; ++row) {
        gl->texSubImage2D(glTexture->target(), 0, x, y + row, width, 1,
                          textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                          data + (static_cast<size_t>(row) * rowBytes));
      }
    }
  }
}

bool GLCommandQueue::readPixels(GPUFrameBuffer* frameBuffer, const Rect& rect, void* pixels,
                                size_t rowBytes) const {
  if (frameBuffer == nullptr || rect.isEmpty() || pixels == nullptr) {
    return false;
  }
  auto format = frameBuffer->format();
  auto bytesPerPixel = PixelFormatBytesPerPixel(format);
  auto minRowBytes = static_cast<size_t>(rect.width()) * bytesPerPixel;
  if (rowBytes < minRowBytes) {
    LOGE("GLCommandQueue::readPixels() rowBytes is too small!");
    return false;
  }
  auto gl = interface->functions();
  auto caps = interface->caps();
  ClearGLError(gl);
  auto glFrameBuffer = static_cast<GLFrameBuffer*>(frameBuffer);
  gl->bindFramebuffer(GL_FRAMEBUFFER, glFrameBuffer->readFrameBufferID());
  void* outPixels = nullptr;
  Buffer tempBuffer = {};
  auto restoreGLRowLength = false;
  if (rowBytes == minRowBytes) {
    outPixels = pixels;
  } else if (caps->packRowLengthSupport) {
    outPixels = pixels;
    gl->pixelStorei(GL_PACK_ROW_LENGTH, static_cast<int>(rowBytes / bytesPerPixel));
    restoreGLRowLength = true;
  } else {
    tempBuffer.alloc(minRowBytes * static_cast<size_t>(rect.height()));
    outPixels = tempBuffer.data();
  }
  gl->pixelStorei(GL_PACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  auto x = static_cast<int>(rect.left);
  auto y = static_cast<int>(rect.top);
  auto width = static_cast<int>(rect.width());
  auto height = static_cast<int>(rect.height());
  auto textureFormat = caps->getTextureFormat(format);
  gl->readPixels(x, y, width, height, textureFormat.externalFormat, GL_UNSIGNED_BYTE, outPixels);
  if (restoreGLRowLength) {
    gl->pixelStorei(GL_PACK_ROW_LENGTH, 0);
  }
  if (!tempBuffer.isEmpty()) {
    auto srcPixels = static_cast<const uint8_t*>(outPixels);
    auto dstPixels = static_cast<uint8_t*>(pixels);
    auto rowCount = static_cast<size_t>(rect.height());
    for (size_t row = 0; row < rowCount; ++row) {
      memcpy(dstPixels, srcPixels, minRowBytes);
      dstPixels += rowBytes;
      srcPixels += minRowBytes;
    }
  }
  return CheckGLError(gl);
}

void GLCommandQueue::submit(std::shared_ptr<CommandBuffer>) {
  auto gl = interface->functions();
  gl->flush();
}

void GLCommandQueue::waitUntilCompleted() {
  auto gl = interface->functions();
  gl->finish();
}

}  // namespace tgfx
