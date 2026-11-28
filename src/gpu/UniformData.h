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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/ir/ProgramIR.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

struct InterfaceIR;

/**
 * One stage's uniform block: the byte size of the block and the member table the uploads address.
 * The layout is read from the program IR, the single place the offsets are assigned. A write names
 * the uniform by its base name and is resolved against the owner the upload is currently writing
 * for, so two processors that declare the same base name cannot land in each other's slot.
 */
class UniformData {
 public:
  /**
   * Builds the block from the IR the program already carries: offsets and sizes are copied, never
   * recomputed. Returns null when the stage declares no block, and refuses (null) when a stage
   * declares more than one, because the per-block name mapping does not exist yet.
   */
  static std::unique_ptr<UniformData> Make(const InterfaceIR& interface, ShaderStage stage);

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

  /// Sets the owner the following writes resolve against: the index of the processor whose
  /// uniforms are being written, or NO_UNIFORM_OWNER for the program-level values (RTAdjust).
  void setOwner(uint32_t newOwner);

  /// Whether a write missed its member or overflowed it. The program build fails on either.
  bool hasUploadError() const;

  /// The number of declared members no write has reached yet.
  size_t unwrittenCount() const;

  /// The declared names (mangled) no write has reached yet, for diagnostics.
  std::vector<std::string> unwrittenNames() const;

 private:
  struct Field {
    std::string name = {};
    uint32_t owner = NO_UNIFORM_OWNER;
    size_t offset = 0;
    size_t size = 0;
    mutable bool written = false;
  };

  UniformData() = default;

  uint8_t* _buffer = nullptr;
  size_t bufferSize = 0;
  uint32_t owner = NO_UNIFORM_OWNER;
  std::unordered_map<std::string, std::vector<Field>> byBaseName = {};
  mutable bool uploadError = false;

  void onSetData(const std::string& baseName, const void* data, size_t size) const;

  /// The member named by the base name and owned by the current owner, or null.
  const Field* findField(const std::string& baseName) const;

  friend class ProgramInfo;
  friend class UniformHandler;
};
}  // namespace tgfx
