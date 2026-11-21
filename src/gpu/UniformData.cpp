/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "UniformData.h"
#include "core/utils/Log.h"

namespace tgfx {

UniformData::UniformData(size_t size) : bufferSize(size) {
}

UniformData::UniformData(std::vector<Uniform> uniforms) : _uniforms(std::move(uniforms)) {
  for (const auto& uniform : _uniforms) {
    const auto& [entrySize, entryAlign] = EntryOf(uniform.format());
    const auto count = uniform.count();
    // The std140 layout aligns every array element to a 16-byte boundary, so only formats whose
    // element size is 16 bytes (e.g. Float4) match the contiguous CPU-side layout. Other element
    // types would be misaligned in the GPU uniform block and must not be used as arrays.
    DEBUG_ASSERT(count <= 1 || entrySize == 16);
    const size_t size = entrySize * count;
    const size_t align = entryAlign;

    const size_t offset = alignCursor(align);
    fieldMap[uniform.name()] = {uniform.name(), uniform.format(), offset, size, align};
    cursor = offset + size;
  }

  bufferSize = alignCursor(16);
}

void UniformData::setBuffer(void* buffer) {
  _buffer = static_cast<uint8_t*>(buffer);
}

void UniformData::onSetData(const std::string& name, const void* data, size_t size) const {
  DEBUG_ASSERT(_buffer != nullptr);

  const auto& key = name + nameSuffix;
  auto field = findField(key);

  if (field == nullptr) {
    LOGE("UniformData::onSetData() uniform '%s' not found!", name.c_str());
    return;
  }
  DEBUG_ASSERT(field->size == size);

  memcpy(_buffer + field->offset, data, size);
}

const UniformData::Field* UniformData::findField(const std::string& key) const {
  const auto& iter = fieldMap.find(key);
  if (iter != fieldMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

size_t UniformData::alignCursor(size_t alignment) const {
  return (cursor + alignment - 1) / alignment * alignment;
}

UniformData::Entry UniformData::EntryOf(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return {4, 4};
    case UniformFormat::Float2:
      return {8, 8};
    case UniformFormat::Float3:
      return {12, 16};
    case UniformFormat::Float4:
      return {16, 16};
    case UniformFormat::Float2x2:
      return {32, 16};
    case UniformFormat::Float3x3:
      return {48, 16};
    case UniformFormat::Float4x4:
      return {64, 16};
    case UniformFormat::Int:
      return {4, 4};
    case UniformFormat::Int2:
      return {8, 8};
    case UniformFormat::Int3:
      return {12, 16};
    case UniformFormat::Int4:
      return {16, 16};
    default:
      return {0, 0};
  }
}

}  // namespace tgfx
