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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/Uniform.h"
#include "gpu/aot/bind/AOTParamLayout.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

struct AOTProgram;

/**
 * An object representing the collection of uniform data in CPU side.
 */
class UniformData {
 public:
  /**
   *
   * Copies value into the uniform data. The data must have the same size as the uniform specified
   * by name.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix> &&
                       !std::is_same_v<std::decay_t<T>, ColorMatrix33>,
                   void>
  setData(const std::string& name, const T& value) const {
    onSetData(name, &value, sizeof(value));
  }

  /**
   * Like setData(), but for a uniform that may legitimately be absent from this program. Precompiled
   * shaders can declare uniforms (e.g. a Kernel's tile-mode) that the runtime-generated shader does
   * not, because the runtime path expresses the same behaviour through generated code instead. When
   * the field is absent this silently does nothing rather than logging an error, so a required-field
   * typo is still caught while an intentionally-optional uniform is tolerated.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix> &&
                       !std::is_same_v<std::decay_t<T>, ColorMatrix33>,
                   void>
  setDataOptional(const std::string& name, const T& value) const {
    onSetData(name, &value, sizeof(value), true);
  }

  /**
   * Convenience method for copying a Matrix to a 3x3 matrix in column-major order.
   */
  template <typename T>
  std::enable_if_t<std::is_same_v<std::decay_t<T>, Matrix>, void> setData(const std::string& name,
                                                                          const T& matrix) const {
    float values[9] = {};
    matrix.get9(values);

    // clang-format off
    const float data[] = {
      values[0], values[3], values[6], 0,
      values[1], values[4], values[7], 0,
      values[2], values[5], values[8], 0,
    };
    // clang-format on
    onSetData(name, data, sizeof(data));
  }

  /**
   * Like setData(), but tolerates the matrix uniform being absent from this program (see
   * setDataOptional() for when absence is legitimate).
   */
  template <typename T>
  std::enable_if_t<std::is_same_v<std::decay_t<T>, Matrix>, void> setDataOptional(
      const std::string& name, const T& matrix) const {
    float values[9] = {};
    matrix.get9(values);
    const float data[] = {
        values[0], values[3], values[6], 0,         values[1], values[4],
        values[7], 0,         values[2], values[5], values[8], 0,
    };
    onSetData(name, data, sizeof(data), true);
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<std::decay_t<T>, ColorMatrix33>, void> setData(
      const std::string& name, const T& matrix) const {

    // clang-format off
      const float data[] = {
        matrix.values[0][0], matrix.values[1][0], matrix.values[2][0], 0,
        matrix.values[0][1], matrix.values[1][1], matrix.values[2][1], 0,
        matrix.values[0][2], matrix.values[1][2], matrix.values[2][2], 0,
      };
    // clang-format on
    onSetData(name, data, sizeof(data));
  }

  /**
   * Copies value into one element of an array uniform. A scalar uniform is accepted as a
   * single-element target (only index 0 is in range), so callers can use one call form for both
   * scalar fields and array elements. The value size must match one element.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix> &&
                       !std::is_same_v<std::decay_t<T>, ColorMatrix33>,
                   void>
  setArrayElement(const std::string& name, size_t index, const T& value) const {
    onSetArrayElement(name, index, &value, sizeof(value), false);
  }

  /**
   * Like setArrayElement(), but tolerates the array uniform being absent from this program (see
   * setDataOptional() for when absence is legitimate).
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                       !std::is_same_v<std::decay_t<T>, Matrix> &&
                       !std::is_same_v<std::decay_t<T>, ColorMatrix33>,
                   void>
  setArrayElementOptional(const std::string& name, size_t index, const T& value) const {
    onSetArrayElement(name, index, &value, sizeof(value), true);
  }

  /**
   * Resolves this data's parameter layout (see AOTParamLayout) from its reflected fields. Built
   * once, for precompiled programs only; runtime-generated programs keep no layout and their
   * writes stay name-addressed.
   */
  void buildParamLayout();

  /**
   * The offset-addressed write: the parameter's slot (resolved once per program) and the
   * instance index address the bytes directly — no name composition, no hash lookup. Returns
   * false when this data has no layout, the key is unknown, the parameter is undeclared here,
   * the instance is out of range, or the value's size disagrees; the caller then keeps the
   * name-addressed path, which stays authoritative.
   */
  bool writeParamKey(const void* key, size_t instance, const void* data, size_t size) const;

  /// The Matrix form of writeParamKey, matching setDataOptional's 12-float aligned layout.
  bool writeParamKey(const void* key, size_t instance, const Matrix& matrix) const;

  /**
   * Sets an external memory buffer for writing uniform data.
   * On platforms with UBO support, the buffer points to memory mapped from a GPU UBO object.
   * On platforms without UBO support, the buffer points to CPU memory.
   */
  void setBuffer(void* buffer);

  /**
   * Returns the size of the uniform data in bytes.
   */
  size_t size() const {
    return bufferSize;
  }

  /**
   * Returns the list of uniforms in this data.
   */
  const std::vector<Uniform>& uniforms() const {
    return _uniforms;
  }

  /**
   * Returns true if a uniform field with the given name exists in this data.
   */
  bool hasField(const std::string& name) const;

  /**
   * Reads one 32-bit word of a uniform field back from the current buffer, addressing the field
   * by name and the word by its index within it (a scalar holds one word, an ivec4 array four
   * words per element). The specialization capture reads what the writers wrote, so the values a
   * specialized shader folds and the values the upload writes are one and the same bytes.
   * Returns false when the field is absent, the index is out of range, or no buffer is set.
   */
  bool readInt32(const std::string& name, size_t wordIndex, int32_t* value) const;

 private:
  struct Field {
    std::string name = "";
    UniformFormat format = UniformFormat::Float;
    size_t offset = 0;
    size_t size = 0;
    size_t align = 0;
    uint32_t arraySize = 1;
    size_t elementStride = 0;
    size_t elementSize = 0;
  };

  struct Entry {
    size_t size;
    size_t align;
  };

  uint8_t* _buffer = nullptr;
  size_t bufferSize = 0;
  std::vector<Uniform> _uniforms = {};
  std::unordered_map<std::string, Field> fieldMap = {};
  size_t cursor = 0;
  /// The one memcpy every write funnels into, so the debug write-range tracking sees them all.
  void writeBytes(size_t offset, const void* data, size_t size) const;
  std::unique_ptr<AOTParamLayout> _paramLayout = {};
#if DEBUG
  // The byte ranges this draw's writes covered (reset by setBuffer, which begins a draw's slice):
  // UBO slices are carved from a shared, recycled buffer, so a field no writer touches holds
  // stale bytes from earlier draws. dumpValues() prints those words as '-', so a dumped trace
  // stays deterministic run to run.
  mutable std::vector<std::pair<size_t, size_t>> _debugWriteRanges = {};
#endif

  explicit UniformData(std::vector<Uniform> uniforms);

  void onSetData(const std::string& name, const void* data, size_t size,
                 bool optional = false) const;

  void onSetArrayElement(const std::string& name, size_t index, const void* data, size_t size,
                         bool optional) const;

  const Field* findField(const std::string& key) const;

  size_t alignCursor(size_t alignment) const;

  static Entry EntryOf(UniformFormat format);

  /**
   * Dump UniformData's memory layout information is printed to the console for debugging.
   */
#if DEBUG
  static const char* ToUniformFormatName(UniformFormat format);

  void dump() const;

  // Prints every field's current value (floats/ints read from the backing buffer), for A/B
  // diffing the uniforms two routes upload for the same draw. Words no writer touched this draw
  // print as '-' (recycled-buffer stale bytes). Diagnostic only.
  void dumpValues(const char* label) const;
#endif

  friend class ProgramInfo;
  friend class ProgramWriter;
  friend class AOTParamLayout;
  friend class PrecompiledProgramWriter;
  friend class ChainProgramWriter;
  friend class PrecompiledProgramCreator;
  friend class SpecCapture;
  // The shell-free chain binder drives the same structural-suffix addressing the processor-tree
  // walk maintained; only the per-binding walk needs the direct access.
  friend void BindChainBindings(const AOTProgram& program, UniformData* fragmentUniformData);
};
}  // namespace tgfx
