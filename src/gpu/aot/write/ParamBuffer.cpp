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

#include "gpu/aot/write/ParamBuffer.h"
#include <cstring>
#include "core/utils/Log.h"
#include "gpu/UniformData.h"

namespace tgfx {

ParamBuffer::ParamBuffer(UniformData* vertexData, UniformData* fragmentData)
    : vertexData(vertexData), fragmentData(fragmentData) {
}

void ParamBuffer::write(const Layout::Field& field, const void* data, size_t size) const {
  UniformData* target = field.block == 0 ? vertexData : fragmentData;
  if (target == nullptr || target->writableBytes() == nullptr) {
    return;
  }
  if (static_cast<size_t>(field.offset) + size > target->size()) {
    LOGE("ParamBuffer::write() field at %u (+%zu) exceeds the uniform block of %zu bytes",
         field.offset, size, target->size());
    return;
  }
  std::memcpy(target->writableBytes() + field.offset, data, size);
}

void ParamBuffer::set(const Layout::Field& field, const Matrix& matrix) const {
  float values[9] = {};
  matrix.get9(values);
  // The same column-major 12-float layout the assembled mat3 declaration expects.
  const float data[] = {
      values[0], values[3], values[6], 0,         values[1], values[4],
      values[7], 0,         values[2], values[5], values[8], 0,
  };
  write(field, data, sizeof(data));
}

void ParamBuffer::setArrayElement(const Layout::Field& field, size_t index,
                                  const Matrix& matrix) const {
  float values[9] = {};
  matrix.get9(values);
  const float data[] = {
      values[0], values[3], values[6], 0,         values[1], values[4],
      values[7], 0,         values[2], values[5], values[8], 0,
  };
  writeArrayElement(field, index, data, sizeof(data));
}

void ParamBuffer::writeArrayElement(const Layout::Field& field, size_t index, const void* data,
                                    size_t size) const {
  UniformData* target = field.block == 0 ? vertexData : fragmentData;
  if (target == nullptr || target->writableBytes() == nullptr) {
    return;
  }
  if (index >= field.arraySize) {
    LOGE("ParamBuffer::writeArrayElement() index %zu is out of range for an array of %u elements",
         index, static_cast<unsigned>(field.arraySize));
    return;
  }
  if (size != field.elementSize) {
    LOGE(
        "ParamBuffer::writeArrayElement() element write of %zu bytes does not match the "
        "element's %u-byte size",
        size, static_cast<unsigned>(field.elementSize));
    return;
  }
  // std140 strides every array element to a 16-byte multiple; the field's whole footprint was
  // derived with that same rule, so the stride is exact without a second layout table.
  const size_t stride = field.arraySize > 1 ? field.size / field.arraySize : field.size;
  const size_t offset = field.offset + index * stride;
  if (offset + size > target->size()) {
    LOGE(
        "ParamBuffer::writeArrayElement() element at %zu (+%zu) exceeds the uniform block of "
        "%zu bytes",
        offset, size, target->size());
    return;
  }
  std::memcpy(target->writableBytes() + offset, data, size);
}

}  // namespace tgfx
