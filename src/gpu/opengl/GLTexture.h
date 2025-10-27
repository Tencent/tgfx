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

#pragma once

#include "gpu/opengl/GLInterface.h"
#include "gpu/opengl/GLResource.h"
#include "gpu/opengl/GLSampler.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
class GLGPU;

/**
 * GLTexture is a Texture that wraps an OpenGL texture, providing access to its OpenGL texture ID
 * and target.
 */
class GLTexture : public Texture, public GLResource {
 public:
  /**
   * Creates a GLTexture with the specified descriptor, OpenGL target, and texture ID.
   */
  GLTexture(const TextureDescriptor& descriptor, unsigned target, unsigned textureID);

  /**
   * Returns the OpenGL target for this texture.
   */
  unsigned target() const {
    return _target;
  }

  /**
   * Returns the OpenGL ID for this texture.
   */
  unsigned textureID() const {
    return _textureID;
  }

  /**
   * Returns the OpenGL ID for the framebuffer associated with this texture. This is used for
   * rendering to the texture.
   */
  virtual unsigned frameBufferID() const;

  /**
   * Checks whether a framebuffer is needed for rendering, and creates one if necessary. Returns
   * true if the framebuffer is successfully created or not needed; returns false otherwise.
   */
  bool checkFrameBuffer(GLGPU* gpu);

  /**
   * Binds the texture to the specified texture unit and applies the sampler parameters.
   */
  void updateSampler(GLGPU* gpu, const GLSampler* sampler);

  TextureType type() const override;

  BackendTexture getBackendTexture() const override;

  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  unsigned _target = GL_TEXTURE_2D;
  unsigned _textureID = 0;

  void onRelease(GLGPU* gpu) final;

  virtual void onReleaseTexture(GLGPU* gpu);

 private:
  uint32_t uniqueID = 0;
  unsigned textureFrameBuffer = 0;
  int lastWrapS = 0;
  int lastWrapT = 0;
  int lastMinFilter = 0;
  int lastMagFilter = 0;

  friend class GLState;
};
}  // namespace tgfx
