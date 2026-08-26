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

#include "UniformData.h"
#include "core/utils/Log.h"

namespace tgfx {
UniformData::UniformData(std::vector<Uniform> uniforms) : _uniforms(std::move(uniforms)) {
  for (const auto& uniform : _uniforms) {
    const auto& [entrySize, entryAlign] = EntryOf(uniform.format());
    uint32_t arraySize = uniform.arraySize();
    // The std140 layout aligns every array element to a 16-byte boundary. Elements smaller than
    // 16 bytes (e.g. Float) are therefore non-contiguous on the GPU side; writers must go through
    // the elementStride-aware paths (onSetArrayElement / slot writers) instead of assuming a
    // packed CPU-side layout.
    const size_t size = entrySize;
    size_t align = entryAlign;

    // std140 arrays stride each element up to a 16-byte multiple.
    size_t elementStride = (size + 15) / 16 * 16;
    size_t totalSize = arraySize > 1 ? elementStride * arraySize : size;
    // std140 arrays are always aligned to a vec4 boundary, even arrays of scalars.
    if (arraySize > 1 && align < 16) {
      align = 16;
    }

    const size_t offset = alignCursor(align);
    Field field = {};
    field.name = uniform.name();
    field.format = uniform.format();
    field.offset = offset;
    field.size = totalSize;
    field.align = align;
    field.arraySize = arraySize;
    field.elementStride = elementStride;
    field.elementSize = size;
    fieldMap[uniform.name()] = field;
    cursor = offset + totalSize;
  }

  bufferSize = alignCursor(16);
}

void UniformData::setBuffer(void* buffer) {
  _buffer = static_cast<uint8_t*>(buffer);
}

void UniformData::onSetArrayElement(const std::string& name, size_t index, const void* data,
                                    size_t size, bool optional) const {
  DEBUG_ASSERT(_buffer != nullptr);

  const auto& key = skipSuffix ? name + structuralSuffix : name + nameSuffix;
  auto field = findField(key);
  // Scalars are accepted as single-element targets (index 0), so writers can use one call form
  // for both scalar fields and array elements.
  if (field == nullptr || index >= field->arraySize || field->elementSize != size) {
    if (!optional) {
      LOGE(
          "UniformData::onSetArrayElement() array uniform '%s' not found or index %zu out of "
          "range!",
          name.c_str(), index);
    }
    return;
  }
  memcpy(_buffer + field->offset + index * field->elementStride, data, size);
}

void UniformData::onSetData(const std::string& name, const void* data, size_t size,
                            bool optional) const {
  DEBUG_ASSERT(_buffer != nullptr);

  const auto& key = skipSuffix ? name + structuralSuffix : name + nameSuffix;
  auto field = findField(key);

  if (field == nullptr) {
    // A required uniform that is missing indicates a bug (e.g. a name typo), so it is logged. An
    // optional uniform may be intentionally absent (present in the precompiled shader but not the
    // runtime one), so it is silently ignored.
    if (!optional) {
      LOGE("UniformData::onSetData() uniform '%s' not found!", name.c_str());
    }
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

bool UniformData::hasField(const std::string& name) const {
  const auto& key = skipSuffix ? name + structuralSuffix : name + nameSuffix;
  return findField(key) != nullptr;
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
    case UniformFormat::Texture2DSampler:
    case UniformFormat::TextureExternalSampler:
    case UniformFormat::Texture2DRectSampler:
      return {4, 4};
    default:
      return {0, 0};
  }
}

#if DEBUG
const char* UniformData::ToUniformFormatName(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return "Float";
    case UniformFormat::Float2:
      return "Float2";
    case UniformFormat::Float3:
      return "Float3";
    case UniformFormat::Float4:
      return "Float4";
    case UniformFormat::Float2x2:
      return "Float2x2";
    case UniformFormat::Float3x3:
      return "Float3x3";
    case UniformFormat::Float4x4:
      return "Float4x4";
    case UniformFormat::Int:
      return "Int";
    case UniformFormat::Int2:
      return "Int2";
    case UniformFormat::Int3:
      return "Int3";
    case UniformFormat::Int4:
      return "Int4";
    case UniformFormat::Texture2DSampler:
      return "Texture2DSampler";
    case UniformFormat::TextureExternalSampler:
      return "TextureExternalSampler";
    case UniformFormat::Texture2DRectSampler:
      return "Texture2DRectSampler";
    default:
      return "?";
  }
}

void UniformData::dump() const {
  LOGI("\n-------------- UniformData Layout dump begin --------------");
  std::vector<Field> sortedFields;
  sortedFields.reserve(fieldMap.size());
  for (const auto& [name, field] : fieldMap) {
    sortedFields.push_back(field);
  }
  std::sort(sortedFields.begin(), sortedFields.end(),
            [](const Field& a, const Field& b) { return a.offset < b.offset; });
  for (size_t i = 0; i < sortedFields.size(); ++i) {
    LOGI("%4zu: %-10s offset=%4zu, size=%4zu, align=%2zu, name=%s", i,
         ToUniformFormatName(sortedFields[i].format), sortedFields[i].offset, sortedFields[i].size,
         sortedFields[i].align, sortedFields[i].name.c_str());
  }
  LOGI("Total buffer size = %zu bytes", size());
  LOGI("-------------- UniformData Layout dump end --------------\n");
}
#endif
}  // namespace tgfx
