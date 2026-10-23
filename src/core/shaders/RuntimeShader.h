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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "core/utils/UniqueID.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

#define DEFINE_RUNTIME_SHADER_CLASS_ID          \
  static uint32_t ClassID() {                   \
    static uint32_t classID = UniqueID::Next(); \
    return classID;                             \
  }

/**
 * Base class for shaders implemented outside the core module. A Runtime shader reports the
 * Runtime type, which the core treats as opaque: GPU rendering goes through the subclass's
 * asFragmentProcessor, while the SVG and PDF export backends rasterize it instead of mapping it
 * to native primitives.
 *
 * Subclasses must add DEFINE_RUNTIME_SHADER_CLASS_ID to their public declaration, pass ClassID()
 * to the RuntimeShader constructor, and check classID() in isEqual before downcasting the
 * argument.
 */
class RuntimeShader : public Shader {
 public:
  /**
   * Returns the process-wide unique ID of the Runtime shader subclass. The ID is assigned from a
   * counter at runtime, so it is only valid for in-process comparisons and must not be persisted.
   */
  uint32_t classID() const {
    return _classID;
  }

 protected:
  explicit RuntimeShader(uint32_t classID) : _classID(classID) {
  }

 private:
  Type type() const final {
    return Type::Runtime;
  }

  uint32_t _classID = 0;
};
}  // namespace tgfx
