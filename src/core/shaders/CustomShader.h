/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "core/utils/UniqueID.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

#define DEFINE_CUSTOM_SHADER_CLASS_ID           \
  static uint32_t ClassID() {                   \
    static uint32_t classID = UniqueID::Next(); \
    return classID;                             \
  }

/**
 * Base class for shaders outside the built-in shader types, identified by a unique class ID.
 *
 * Subclasses must add DEFINE_CUSTOM_SHADER_CLASS_ID to their public declaration and pass
 * ClassID() to the CustomShader constructor.
 */
class CustomShader : public Shader {
 public:
  /**
   * Returns the unique ID of the custom shader subclass, valid only within the current process.
   * The ID is assigned from a counter at runtime and must not be persisted.
   */
  uint32_t classID() const {
    return _classID;
  }

 protected:
  explicit CustomShader(uint32_t classID) : _classID(classID) {
  }

 private:
  Type type() const final {
    return Type::Custom;
  }

  uint32_t _classID = 0;
};
}  // namespace tgfx
