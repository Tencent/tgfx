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

#include "gpu/opengl/GLTexture.h"
#include <memory>
#include "GLCaps.h"
#include "GLGPU.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
TextureType GLTexture::type() const {
  switch (_target) {
    case GL_TEXTURE_2D:
      return TextureType::TwoD;
    case GL_TEXTURE_RECTANGLE:
      return TextureType::Rectangle;
    case GL_TEXTURE_EXTERNAL_OES:
      return TextureType::External;
    default:
      return TextureType::None;
  }
}

BackendTexture GLTexture::getBackendTexture(int width, int height) const {
  GLTextureInfo textureInfo = {};
  textureInfo.id = _id;
  textureInfo.target = _target;
  textureInfo.format = PixelFormatToGLSizeFormat(_format);
  return {textureInfo, width, height};
}

void GLTexture::computeTextureKey(Context* context, BytesKey* bytesKey) const {
  auto caps = GLCaps::Get(context);
  DEBUG_ASSERT(caps != nullptr);
  bytesKey->write(static_cast<uint32_t>(caps->getReadSwizzle(_format).asKey()));
  bytesKey->write(_target);
}

void GLTexture::release(GPU* gpu) {
  if (gpu == nullptr || _id == 0) {
    return;
  }
  auto gl = static_cast<GLGPU*>(gpu)->functions();
  gl->deleteTextures(1, &_id);
  _id = 0;
}
}  // namespace tgfx