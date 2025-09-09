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
#include "GLReadTexture.h"
#include "GLTexture.h"

namespace tgfx {
class EGLReadTexture : public GLReadTexture {
 public:
  explicit EGLReadTexture(GLTexture* texture) : GLReadTexture(texture) {
  }

  void readTexture(GLGPU* gpu, const Rect& rect, void* pixels) override;

  bool isSupportReadBack(GLGPU* gpu) override;
};

void EGLReadTexture::readTexture(GLGPU* gpu, const Rect& rect, void* pixels) {
  auto gl = gpu->functions();
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  auto x = static_cast<int>(rect.left);
  auto y = static_cast<int>(rect.top);
  auto width = static_cast<int>(rect.width());
  auto height = static_cast<int>(rect.height());
  auto textureFormat = caps->getTextureFormat(texture->format());
  gl->readPixels(x, y, width, height, textureFormat.externalFormat, GL_UNSIGNED_BYTE, pixels);
}

bool EGLReadTexture::isSupportReadBack(GLGPU* gpu) {
  if (texture->usage() & GPUTextureUsage::RENDER_ATTACHMENT) {
    gpu->bindFramebuffer(texture);
    return true;
  }
  if (texture->usage() & GPUTextureUsage::TEXTURE_BINDING) {
    if (!texture->checkFrameBuffer(gpu)) {
      return false;
    }
    return true;
  }
  LOGE("EGLReadTexture texture usage does not support readback!");
  return false;
}

class NativeGLReadTexture : public GLReadTexture {
 public:
  explicit NativeGLReadTexture(GLTexture* texture) : GLReadTexture(texture) {
  }

  void readTexture(GLGPU* gpu, const Rect& rect, void* pixels) override;

  bool isSupportReadBack(GLGPU* gpu) override;
};

void NativeGLReadTexture::readTexture(GLGPU* gpu, const Rect&, void* pixels) {
  auto gl = gpu->functions();
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  gl->bindTexture(texture->target(), texture->textureID());
  auto textureFormat = caps->getTextureFormat(texture->format());
  gl->getTexImage(texture->target(), 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE, pixels);
  gl->bindTexture(texture->target(), 0);
}

bool NativeGLReadTexture::isSupportReadBack(GLGPU*) {
  return true;
}

std::shared_ptr<GLReadTexture> GLReadTexture::MakeFrom(const GLCaps* caps, const Rect& rect,
                                                       GLTexture* texture) {
  auto width = static_cast<int>(rect.width());
  auto height = static_cast<int>(rect.height());
  if (caps->standard == GLStandard::GL && width == texture->width() &&
      height == texture->height() && rect.left == 0 && rect.top == 0) {
    return std::make_shared<NativeGLReadTexture>(texture);
  }
  return std::make_shared<EGLReadTexture>(texture);
}

}  // namespace tgfx