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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cstring>
#include <vector>
#include "gpu/shaders/params/ParamHandle.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

// One instance's fields resolved against the program's fragment uniform block: the module's
// declarations, the active mask, and — parallel to the declarations — the byte offset of each
// field (NO_FIELD when this program's variant does not declare it) and the array element stride.
struct ResolvedInstance {
  static constexpr size_t NO_FIELD = static_cast<size_t>(-1);

  FieldMask mask;
  std::vector<ParamDesc> descs;
  std::vector<size_t> offsets;
  std::vector<size_t> strides;
};

// Writes one module instance's parameters through a resolved instance: no string lookups, no
// suffix guessing — every field's offset was resolved once when the program was created. A field
// outside the instance's mask or absent from the program's variant is skipped (the plan-level
// equivalent of setDataOptional); a wrong value type is a compile error at the call.
class ParamWriter {
 public:
  ParamWriter(uint8_t* buffer, const ResolvedInstance* instance)
      : buffer(buffer), instance(instance) {
  }

  void set(const Param<ParamType::Float>& param, float value) {
    write(param.index, &value, 4);
  }

  void set(const Param<ParamType::Float2>& param, const float value[2]) {
    write(param.index, value, 8);
  }

  void set(const Param<ParamType::Float3>& param, const float value[3]) {
    write(param.index, value, 12);
  }

  void set(const Param<ParamType::Float4>& param, const float value[4]) {
    write(param.index, value, 16);
  }

  void set(const Param<ParamType::Int>& param, int32_t value) {
    write(param.index, &value, 4);
  }

  void set(const Param<ParamType::Float3x3>& param, const Matrix& matrix) {
    // The same padded layout UniformData::setData(const Matrix&) uploads: three vec4 columns.
    float values[9] = {};
    matrix.get9(values);
    const float data[12] = {values[0], values[3], values[6], 0,         values[1], values[4],
                            values[7], 0,         values[2], values[5], values[8], 0};
    write(param.index, data, 48);
  }

  // Array forms: one element per chain slot, addressed by the resolved stride.
  void set(const Param<ParamType::Float4Array>& param, size_t slot, const float value[4]) {
    writeElement(param.index, slot, value, 16);
  }

  void set(const Param<ParamType::IntArray>& param, size_t slot, int32_t value) {
    writeElement(param.index, slot, &value, 4);
  }

 private:
  void write(uint32_t index, const void* data, size_t size) {
    if (buffer == nullptr || instance == nullptr || !instance->mask.test(index)) {
      return;
    }
    if (index >= instance->offsets.size() ||
        instance->offsets[index] == ResolvedInstance::NO_FIELD) {
      return;
    }
    memcpy(buffer + instance->offsets[index], data, size);
  }

  void writeElement(uint32_t index, size_t slot, const void* data, size_t elementSize) {
    if (buffer == nullptr || instance == nullptr || !instance->mask.test(index)) {
      return;
    }
    if (index >= instance->offsets.size() || index >= instance->strides.size() ||
        instance->offsets[index] == ResolvedInstance::NO_FIELD) {
      return;
    }
    memcpy(buffer + instance->offsets[index] + slot * instance->strides[index], data, elementSize);
  }

  uint8_t* buffer = nullptr;
  const ResolvedInstance* instance = nullptr;
};

}  // namespace tgfx
