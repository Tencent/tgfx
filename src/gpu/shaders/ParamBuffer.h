/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <type_traits>
#include "gpu/shaders/Layout.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class UniformData;

/**
 * The layout-addressed write: a derived field says which uniform block and which offset the
 * value goes to, so the runtime never composes a name or looks one up. The buffer routes by the
 * field's block.
 */
class ParamBuffer {
 public:
  ParamBuffer(UniformData* vertexData, UniformData* fragmentData);

  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix>,
                   void>
  set(const Layout::Field& field, const T& value) const {
    write(field, &value, sizeof(value));
  }

  /// The Matrix form, matching UniformData's 12-float column-major layout for a mat3 field.
  void set(const Layout::Field& field, const Matrix& matrix) const;

 private:
  void write(const Layout::Field& field, const void* data, size_t size) const;

  UniformData* vertexData = nullptr;
  UniformData* fragmentData = nullptr;
};

}  // namespace tgfx
