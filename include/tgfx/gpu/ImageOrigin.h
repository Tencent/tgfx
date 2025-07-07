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
 * Textures and render targets can be stored such that (0, 0) in texture space may correspond to
 * either the top-left or bottom-left content pixel.
 */
enum class ImageOrigin {
  /**
   * The default origin of the native coordinate system in the GPU backend. For example, the
   * ImageOrigin::TopLeft is actually the bottom-left origin in the OpenGL coordinate system for
   * textures. Textures newly created by the backend API for off-screen rendering usually have an
   * ImageOrigin::TopLeft origin.
   */
  TopLeft,

  /**
   * Use this origin to flip the content on the y-axis if the GPU backend has a different origin to
   * your system views. It is usually used for on-screen rendering.
   */
  BottomLeft
};
}  // namespace tgfx
