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

#pragma once

namespace tgfx {
/**
 * Defines pixel formats for YUV textures.
 */
enum class YUVFormat {
  /**
   * uninitialized.
   */
  Unknown,
  /**
   * 8-bit Y plane followed by 8 bit 2x2 subsampled U and V planes.
   */
  I420,
  /**
   * 8-bit Y plane followed by an interleaved U/V plane with 2x2 subsampling.
   */
  NV12
};
}  // namespace tgfx
