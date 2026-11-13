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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace tgfx {
/**
 * Uniform variable formats.
 *
 * One value per shader type the declaration layer can express (see ShaderType in
 * gpu/shaders/Module.h): the derived layout stores it per field and the pipeline descriptor
 * compares it against the geometry's vertex attribute formats.
 */
enum class UniformFormat {
  Float = 0,                    // 32-bit floating point scalar.
  Float2 = 1,                   // 2-component vector of 32-bit floating point values.
  Float3 = 2,                   // 3-component vector of 32-bit floating point values.
  Float4 = 3,                   // 4-component vector of 32-bit floating point values.
  Float2x2 = 4,                 // 2x2 matrix of 32-bit floating point values.
  Float3x3 = 5,                 // 3x3 matrix of 32-bit floating point values.
  Float4x4 = 6,                 // 4x4 matrix of 32-bit floating point values.
  Int = 7,                      // 32-bit signed integer scalar.
  Int2 = 8,                     // 2-component vector of 32-bit signed integer values.
  Int3 = 9,                     // 3-component vector of 32-bit signed integer values.
  Int4 = 10,                    // 4-component vector of 32-bit signed integer values.
  Texture2DSampler = 11,        // 2D texture sampler.
  TextureExternalSampler = 12,  // External texture sampler (e.g. for camera input).
  Texture2DRectSampler = 13,    // Rectangle texture sampler.
};

static_assert(static_cast<int>(UniformFormat::Float) == 0 &&
                  static_cast<int>(UniformFormat::Float4x4) == 6 &&
                  static_cast<int>(UniformFormat::Int) == 7 &&
                  static_cast<int>(UniformFormat::Int4) == 10 &&
                  static_cast<int>(UniformFormat::Texture2DSampler) == 11 &&
                  static_cast<int>(UniformFormat::Texture2DRectSampler) == 13,
              "the format values are pinned; append new formats instead of renumbering");

}  // namespace tgfx
