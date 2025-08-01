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

namespace tgfx {
/**
 * Types for interacting with GL textures created externally to TGFX.
 */
struct GLTextureInfo {
  /**
   * the id of this texture.
   */
  unsigned id = 0;
  /**
   * The target of this texture.
   */
  unsigned target = 0x0DE1;  // GL_TEXTURE_2D;
  /**
   * The pixel format of this texture.
   */
  unsigned format = 0x8058;  // GL_RGBA8;
};

/**
 * Types for interacting with GL frame buffers created externally to tgfx.
 */
struct GLFrameBufferInfo {
  /**
   * The id of this frame buffer.
   */
  unsigned id = 0;

  /**
   * The pixel format of this frame buffer.
   */
  unsigned format = 0x8058;  // GL_RGBA8;
};

/**
 * Types for interacting with GL sync objects created externally to TGFX.
 */
struct GLSyncInfo {
  /*
   * The GL sync object used for synchronization.
   */
  void* sync = nullptr;
};
}  // namespace tgfx
