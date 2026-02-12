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

#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/metal/MtlTypes.h"
#include "tgfx/gpu/opengl/GLTypes.h"

namespace tgfx {
/**
 * Possible GPU backend APIs that may be used by TGFX.
 */
enum class Backend { Unknown, OpenGL, Metal, Vulkan, WebGPU };

/**
 * Wrapper class for passing into and receiving data from TGFX about a backend texture object.
 */
class BackendTexture {
 public:
  /**
   * Creates an invalid backend texture.
   */
  BackendTexture() : _width(0), _height(0) {
  }

  /**
   * Creates an OpenGL backend texture.
   */
  BackendTexture(const GLTextureInfo& glInfo, int width, int height)
      : _backend(Backend::OpenGL), _width(width), _height(height), glInfo(glInfo) {
  }

  /**
   * Creates a Metal backend texture.
   */
  BackendTexture(const MtlTextureInfo& mtlInfo, int width, int height)
      : _backend(Backend::Metal), _width(width), _height(height), mtlInfo(mtlInfo) {
  }

  BackendTexture(const BackendTexture& that) {
    *this = that;
  }

  BackendTexture& operator=(const BackendTexture& that);

  /**
   * Returns true if the backend texture has been initialized.
   */
  bool isValid() const {
    return _width > 0 && _height > 0;
  }

  /**
   * Returns the width of the texture.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the texture.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the backend API of this texture.
   */
  Backend backend() const {
    return _backend;
  }

  /**
   * Returns the pixel format of this texture.
   */
  PixelFormat format() const;

  /**
   * If the backend API is GL, copies a snapshot of the GLTextureInfo struct into the passed in
   * pointer and returns true. Otherwise, returns false if the backend API is not GL.
   */
  bool getGLTextureInfo(GLTextureInfo* glTextureInfo) const;

  /**
   * If the backend API is Metal, copies a snapshot of the GrMtlTextureInfo struct into the passed
   * in pointer and returns true. Otherwise, returns false if the backend API is not Metal.
   */
  bool getMtlTextureInfo(MtlTextureInfo* mtlTextureInfo) const;

 private:
  Backend _backend = Backend::Unknown;
  int _width = 0;
  int _height = 0;

  union {
    GLTextureInfo glInfo;
    MtlTextureInfo mtlInfo;
  };
};

/**
 * Wrapper class for passing into and receiving data from TGFX about a backend render target object.
 */
class BackendRenderTarget {
 public:
  /**
   * Creates an invalid backend render target.
   */
  BackendRenderTarget() : _width(0), _height(0) {
  }

  /**
   * Creates an OpenGL backend render target.
   */
  BackendRenderTarget(const GLFrameBufferInfo& glInfo, int width, int height)
      : _backend(Backend::OpenGL), _width(width), _height(height), glInfo(glInfo) {
  }

  /**
   * Creates an Metal backend render target.
   */
  BackendRenderTarget(const MtlTextureInfo& mtlInfo, int width, int height)
      : _backend(Backend::Metal), _width(width), _height(height), mtlInfo(mtlInfo) {
  }

  BackendRenderTarget(const BackendRenderTarget& that) {
    *this = that;
  }

  BackendRenderTarget& operator=(const BackendRenderTarget&);

  /**
   * Returns true if the backend texture has been initialized.
   */
  bool isValid() const {
    return _width > 0 && _height > 0;
  }

  /**
   * Returns the width of this render target.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of this render target.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the backend API of this render target.
   */
  Backend backend() const {
    return _backend;
  }

  /**
   * Returns the pixel format of this render target.
   */
  PixelFormat format() const;

  /**
   * If the backend API is GL, copies a snapshot of the GLFramebufferInfo struct into the passed
   * in pointer and returns true. Otherwise, returns false if the backend API is not GL.
   */
  bool getGLFramebufferInfo(GLFrameBufferInfo* glFrameBufferInfo) const;

  /**
   * If the backend API is Metal, copies a snapshot of the MtlTextureInfo struct into the passed
   * in pointer and returns true. Otherwise, returns false if the backend API is not Metal.
   */
  bool getMtlTextureInfo(MtlTextureInfo* mtlTextureInfo) const;

 private:
  Backend _backend = Backend::Unknown;
  int _width = 0;
  int _height = 0;
  union {
    GLFrameBufferInfo glInfo;
    MtlTextureInfo mtlInfo;
  };
};

/**
 * Wrapper class for passing into and receiving data from TGFX about a backend semaphore object.
 */
class BackendSemaphore {
 public:
  /**
   * Creates an uninitialized backend semaphore.
   */
  BackendSemaphore() : _backend(Backend::Unknown) {
  }

  /**
   * Creates an OpenGL backend semaphore.
   */
  BackendSemaphore(const GLSyncInfo& syncInfo) : _backend(Backend::OpenGL), glSyncInfo(syncInfo) {
  }

  /**
   * Creates a Metal backend semaphore.
   */
  BackendSemaphore(const MtlSemaphoreInfo& mtlInfo)
      : _backend(Backend::Metal), mtlSemaphoreInfo(mtlInfo) {
  }

  BackendSemaphore(const BackendSemaphore& that) {
    *this = that;
  }

  BackendSemaphore& operator=(const BackendSemaphore&);

  /**
   * Returns true if the backend semaphore has been initialized.
   */
  bool isInitialized() const;

  /**
   * Returns the backend API of this semaphore.
   */
  Backend backend() const {
    return _backend;
  }

  bool getGLSync(GLSyncInfo* syncInfo) const;

  bool getMtlSemaphore(MtlSemaphoreInfo* mtlInfo) const;

 private:
  Backend _backend = Backend::Unknown;
  union {
    GLSyncInfo glSyncInfo;
    MtlSemaphoreInfo mtlSemaphoreInfo;
  };
};
}  // namespace tgfx
