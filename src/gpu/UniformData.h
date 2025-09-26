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
#include <vector>
#include "gpu/Uniform.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
static constexpr char VertexUniformBlockName[] = "VertexUniformBlock";
static constexpr char FragmentUniformBlockName[] = "FragmentUniformBlock";
static constexpr int VERTEX_UBO_BINDING_POINT = 0;
static constexpr int FRAGMENT_UBO_BINDING_POINT = 1;
static constexpr int TEXTURE_BINDING_POINT_START = 2;

/**
 * An object representing the collection of uniform data in CPU side.
 */
class UniformData {
 public:
  /**
   * Copies value into the uniform data. The data must have the same size as the uniform specified
   * by name.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
                 !std::is_same_v<std::decay_t<T>, Matrix>, void> setData(
      const std::string& name, const T& value) const {
    onSetData(name, &value, sizeof(value));
  }

  /**
   * Convenience method for copying a Matrix to a 3x3 matrix in column-major order.
   */
  void setData(const std::string& name, const Matrix& matrix) const;

  /**
   * Sets an external memory buffer for writing uniform data.
   * On platforms with UBO support, the buffer points to memory mapped from a GPU UBO object.
   * On platforms without UBO support, the buffer points to CPU memory.
   * bufferBaseOffset specifies the starting offset in bytes within the buffer.
   */
  void setBuffer(void *buffer, size_t bufferBaseOffset);

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
   * Returns true if UBO is supported in the current context.
   */
  bool uboSupport() const {
    return _uboSupport;
  }

 private:
  struct Field {
    std::string name = "";
    UniformFormat format = UniformFormat::Float;
    size_t offset = 0;
    size_t size = 0;
    size_t align = 0;
  };

  struct Entry {
    size_t size;
    size_t align;
  };

  uint8_t* _buffer = nullptr;
  size_t bufferSize = 0;
  std::vector<Uniform> _uniforms = {};
  std::string nameSuffix = "";
  std::unordered_map<std::string, Field> fieldMap = {};
  size_t cursor = 0;
  bool _uboSupport = false;

  explicit UniformData(std::vector<Uniform> uniforms, bool uboSupport = false);

  void onSetData(const std::string& name, const void* data, size_t size) const;

  const Field* findField(const std::string& key) const;

  size_t alignCursor(size_t alignment) const;

  static Entry EntryOf(UniformFormat format);

  /**
   * Dump UniformData's memory layout information is printed to the console for debugging.
   */
#if DEBUG
  static const char* ToUniformFormatName(UniformFormat format);

  void dump() const;
#endif

  friend class ProgramInfo;
  friend class UniformHandler;
};
}  // namespace tgfx
