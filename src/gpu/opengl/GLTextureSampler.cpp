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

#include "gpu/opengl/GLTextureSampler.h"
#include <memory>
#include "GLCaps.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
class GLExternalTextureSampler : public GLTextureSampler {
 public:
  GLExternalTextureSampler(unsigned id, unsigned target, PixelFormat format)
      : GLTextureSampler(id, target, format) {
  }

  void releaseGPU(Context*) override {
    // External textures are not owned by TGFX, so we do not release them.
  }
};

PixelFormat TextureSampler::GetPixelFormat(const BackendTexture& backendTexture) {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.isValid() || !backendTexture.getGLTextureInfo(&textureInfo)) {
    return PixelFormat::Unknown;
  }
  return GLSizeFormatToPixelFormat(textureInfo.format);
}

std::unique_ptr<TextureSampler> TextureSampler::MakeFrom(Context* context,
                                                         const BackendTexture& backendTexture,
                                                         bool adopted) {
  GLTextureInfo textureInfo = {};
  if (context == nullptr || !backendTexture.isValid() ||
      !backendTexture.getGLTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto format = GLSizeFormatToPixelFormat(textureInfo.format);
  if (adopted) {
    return std::make_unique<GLTextureSampler>(textureInfo.id, textureInfo.target, format);
  }
  return std::make_unique<GLExternalTextureSampler>(textureInfo.id, textureInfo.target, format);
}

std::unique_ptr<TextureSampler> TextureSampler::Make(Context* context, int width, int height,
                                                     PixelFormat format, bool mipmapped) {
  auto gl = GLFunctions::Get(context);
  // Clear the previously generated GLError, causing the subsequent CheckGLError to return an
  // incorrect result.
  ClearGLError(gl);
  unsigned target = GL_TEXTURE_2D;
  unsigned samplerID = 0;
  gl->genTextures(1, &samplerID);
  if (samplerID == 0) {
    return nullptr;
  }
  gl->bindTexture(target, samplerID);
  gl->texParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  const auto& textureFormat = GLCaps::Get(context)->getTextureFormat(format);
  int maxMipmapLevel = mipmapped ? context->caps()->getMaxMipmapLevel(width, height) : 0;
  bool success = true;
  // Texture memory must be allocated first on the web platform then can write pixels.
  for (int level = 0; level <= maxMipmapLevel && success; level++) {
    const int twoToTheMipLevel = 1 << level;
    const int currentWidth = std::max(1, width / twoToTheMipLevel);
    const int currentHeight = std::max(1, height / twoToTheMipLevel);
    gl->texImage2D(target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                   nullptr);
    success = CheckGLError(gl);
  }
  if (!success) {
    gl->deleteTextures(1, &samplerID);
    return nullptr;
  }
  return std::make_unique<GLTextureSampler>(samplerID, target, format, maxMipmapLevel);
}

SamplerType GLTextureSampler::type() const {
  switch (_target) {
    case GL_TEXTURE_2D:
      return SamplerType::TwoD;
    case GL_TEXTURE_RECTANGLE:
      return SamplerType::Rectangle;
    case GL_TEXTURE_EXTERNAL_OES:
      return SamplerType::External;
    default:
      return SamplerType::None;
  }
}

BackendTexture GLTextureSampler::getBackendTexture(int width, int height) const {
  GLTextureInfo textureInfo = {};
  textureInfo.id = _id;
  textureInfo.target = _target;
  textureInfo.format = PixelFormatToGLSizeFormat(_format);
  return {textureInfo, width, height};
}

void GLTextureSampler::writePixels(Context* context, const Rect& rect, const void* pixels,
                                   size_t rowBytes) {
  if (context == nullptr || rect.isEmpty()) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  // https://skia-review.googlesource.com/c/skia/+/571418
  // HUAWEI nova9 pro(Adreno 642L), iqoo neo5(Adreno 650), Redmi K30pro(Adreno 650),
  // Xiaomi 8(Adreno 630), glaxy s9(Adreno 630)
  gl->flush();
  auto caps = GLCaps::Get(context);
  gl->bindTexture(_target, _id);
  const auto& textureFormat = caps->getTextureFormat(_format);
  auto bytesPerPixel = PixelFormatBytesPerPixel(_format);
  gl->pixelStorei(GL_UNPACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  int x = static_cast<int>(rect.x());
  int y = static_cast<int>(rect.y());
  int width = static_cast<int>(rect.width());
  int height = static_cast<int>(rect.height());
  if (caps->unpackRowLengthSupport) {
    // the number of pixels, not bytes
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<int>(rowBytes / bytesPerPixel));
    gl->texSubImage2D(_target, 0, x, y, width, height, textureFormat.externalFormat,
                      GL_UNSIGNED_BYTE, pixels);
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  } else {
    if (static_cast<size_t>(width) * bytesPerPixel == rowBytes) {
      gl->texSubImage2D(_target, 0, x, y, width, height, textureFormat.externalFormat,
                        GL_UNSIGNED_BYTE, pixels);
    } else {
      auto data = reinterpret_cast<const uint8_t*>(pixels);
      for (int row = 0; row < height; ++row) {
        gl->texSubImage2D(_target, 0, x, y + row, width, 1, textureFormat.externalFormat,
                          GL_UNSIGNED_BYTE, data + (static_cast<size_t>(row) * rowBytes));
      }
    }
  }
}

void GLTextureSampler::computeSamplerKey(Context* context, BytesKey* bytesKey) const {
  auto caps = GLCaps::Get(context);
  DEBUG_ASSERT(caps != nullptr);
  bytesKey->write(static_cast<uint32_t>(caps->getReadSwizzle(_format).asKey()));
  bytesKey->write(_target);
}

void GLTextureSampler::releaseGPU(Context* context) {
  if (context == nullptr || _id == 0) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  gl->deleteTextures(1, &_id);
  _id = 0;
}
}  // namespace tgfx