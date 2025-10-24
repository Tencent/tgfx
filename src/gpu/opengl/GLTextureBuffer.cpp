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

#include "GLTextureBuffer.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/GPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLTextureBuffer::GLTextureBuffer(std::shared_ptr<GLInterface> interface,
                                 std::shared_ptr<GLState> state, size_t size)
    : GLBuffer(std::move(interface), 0, size, GPUBufferUsage::READBACK), state(std::move(state)) {
}

void* GLTextureBuffer::map(size_t offset, size_t size) {
  if (texture == nullptr) {
    LOGE("GLTextureBuffer::map() the readback buffer is not initialized!");
    return nullptr;
  }
  if (bufferData != nullptr) {
    LOGE("GLTextureBuffer::map() you must call unmap() before mapping again.");
    return nullptr;
  }
  if (size == 0) {
    LOGE("GLTextureBuffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("GLTextureBuffer::map() range out of bounds!");
    return nullptr;
  }
  bufferData = malloc(_size);
  if (bufferData == nullptr) {
    return nullptr;
  }
  auto gl = _interface->functions();
  ClearGLError(gl);
  state->bindFramebuffer(static_cast<GLTexture*>(texture.get()));
  auto format = texture->format();
  auto bytesPerPixel = PixelFormatBytesPerPixel(format);
  auto minRowBytes = static_cast<size_t>(texture->width()) * bytesPerPixel;
  if (readRowBytes != minRowBytes) {
    gl->pixelStorei(GL_PACK_ROW_LENGTH, static_cast<int>(readRowBytes / bytesPerPixel));
  }
  gl->pixelStorei(GL_PACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  // Clear the pbo binding to avoid interference.
  gl->bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  auto textureFormat = _interface->caps()->getTextureFormat(format);
  auto outPixels = static_cast<uint8_t*>(bufferData) + readOffset;
  gl->readPixels(0, 0, texture->width(), texture->height(), textureFormat.externalFormat,
                 textureFormat.externalType, outPixels);
  if (readRowBytes != minRowBytes) {
    gl->pixelStorei(GL_PACK_ROW_LENGTH, 0);
  }
  if (!CheckGLError(gl)) {
    free(bufferData);
    bufferData = nullptr;
    return nullptr;
  }
  return static_cast<uint8_t*>(bufferData) + offset;
}

void GLTextureBuffer::unmap() {
  if (bufferData == nullptr) {
    return;
  }
  free(bufferData);
  bufferData = nullptr;
}

std::shared_ptr<Texture> GLTextureBuffer::acquireTexture(GPU* gpu,
                                                         std::shared_ptr<Texture> srcTexture,
                                                         const Rect& srcRect, size_t dstOffset,
                                                         size_t dstRowBytes) {
  if (texture == nullptr || texture->width() != srcTexture->width() ||
      texture->height() != srcTexture->height() || texture->format() != srcTexture->format()) {
    TextureDescriptor descriptor(static_cast<int>(srcRect.width()),
                                 static_cast<int>(srcRect.height()), srcTexture->format(), false, 1,
                                 TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT);
    texture = gpu->createTexture(descriptor);
  }
  readOffset = dstOffset;
  readRowBytes = dstRowBytes;
  return texture;
}

}  // namespace tgfx
