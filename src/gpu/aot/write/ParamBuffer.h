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
#include <cstdint>
#include <type_traits>
#include "core/utils/Log.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/Module.h"
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

  /**
   * One element of a std140 array field: the stride is the field's whole footprint divided by its
   * element count (one packing rule derives both), and the write size is validated against the
   * element size, not the stride. The chain's slot pools (`SlotPacked`, `ChainData`, the tiled
   * recipes) are addressed this way.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix>,
                   void>
  setArrayElement(const Layout::Field& field, size_t index, const T& value) const {
    writeArrayElement(field, index, &value, sizeof(value));
  }

  /// The Matrix form of an array element (the 12-float column-major mat3 layout).
  void setArrayElement(const Layout::Field& field, size_t index, const Matrix& matrix) const;

 private:
  void write(const Layout::Field& field, const void* data, size_t size) const;
  void writeArrayElement(const Layout::Field& field, size_t index, const void* data,
                         size_t size) const;

  UniformData* vertexData = nullptr;
  UniformData* fragmentData = nullptr;
};

/**
 * The writes of one module slot, addressed by the element's index in its module's declaration
 * (an ElementIndexOf constant) instead of a name: the layout's elementField table resolves the
 * index to the uniform field once per program, so no string is composed at draw time. The two
 * write forms carry the missing-field contract: `set` is for elements the module declares
 * unconditionally (an absent field is a bug — logged and asserted), `setIfPresent` for elements
 * whose `when` dim legitimately excludes them from this variant.
 */
class SlotWriter {
 public:
  SlotWriter(const Layout& layout, const Module& module, uint8_t slot, ParamBuffer* buffer)
      : layout(layout), module(module), slot(slot), buffer(buffer) {
  }

  template <typename T>
  void set(uint8_t elementIndex, const T& value) const {
    const auto* field = fieldOf(elementIndex);
    if (field == nullptr) {
      return;
    }
    buffer->set(*field, value);
  }

  void set(uint8_t elementIndex, const Matrix& value) const {
    const auto* field = fieldOf(elementIndex);
    if (field == nullptr) {
      return;
    }
    buffer->set(*field, value);
  }

  template <typename T>
  void setIfPresent(uint8_t elementIndex, const T& value) const {
    const uint16_t fieldIndex = fieldIndexOf(elementIndex);
    if (fieldIndex == Layout::NoField) {
      return;
    }
    buffer->set(layout.fields[fieldIndex], value);
  }

  void setIfPresent(uint8_t elementIndex, const Matrix& value) const {
    const uint16_t fieldIndex = fieldIndexOf(elementIndex);
    if (fieldIndex == Layout::NoField) {
      return;
    }
    buffer->set(layout.fields[fieldIndex], value);
  }

  /// One element of an array field, addressed by the element index in the declaration.
  template <typename T>
  void setArrayElement(uint8_t elementIndex, size_t index, const T& value) const {
    const auto* field = fieldOf(elementIndex);
    if (field == nullptr) {
      return;
    }
    buffer->setArrayElement(*field, index, value);
  }

  void setArrayElement(uint8_t elementIndex, size_t index, const Matrix& value) const {
    const auto* field = fieldOf(elementIndex);
    if (field == nullptr) {
      return;
    }
    buffer->setArrayElement(*field, index, value);
  }

  template <typename T>
  void setArrayElementIfPresent(uint8_t elementIndex, size_t index, const T& value) const {
    const uint16_t fieldIndex = fieldIndexOf(elementIndex);
    if (fieldIndex == Layout::NoField) {
      return;
    }
    buffer->setArrayElement(layout.fields[fieldIndex], index, value);
  }

  /// Whether the element contributes a uniform field in this variant (a `when` gate the caller
  /// branches on for the value, like the tiled recipe's swizzle).
  bool present(uint8_t elementIndex) const {
    return fieldIndexOf(elementIndex) != Layout::NoField;
  }

 private:
  uint16_t fieldIndexOf(uint8_t elementIndex) const {
    if (slot >= layout.elementField.size() || elementIndex >= layout.elementField[slot].size()) {
      LOGE("SlotWriter: %s has no element %u; the module and the writer disagree",
           module.name != nullptr ? module.name : "", elementIndex);
      DEBUG_ASSERT(false);
      return Layout::NoField;
    }
    return layout.elementField[slot][elementIndex];
  }

  const Layout::Field* fieldOf(uint8_t elementIndex) const {
    const uint16_t fieldIndex = fieldIndexOf(elementIndex);
    if (fieldIndex == Layout::NoField) {
      LOGE(
          "SlotWriter: %s element %u is absent from the program's layout though the module "
          "declares it unconditionally; the writer and the variant disagree",
          module.name != nullptr ? module.name : "", static_cast<unsigned>(elementIndex));
      DEBUG_ASSERT(false);
      return nullptr;
    }
    return &layout.fields[fieldIndex];
  }

  const Layout& layout;
  const Module& module;
  uint8_t slot;
  ParamBuffer* buffer;
};

}  // namespace tgfx
