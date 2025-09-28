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
UniformData::UniformData(std::vector<Uniform> uniforms, bool uboSupport)
    : _uniforms(std::move(uniforms)), _uboSupport(uboSupport) {
  for (const auto& uniform : _uniforms) {
    size_t size = 0;
    size_t align = 0;
    if (_uboSupport) {
      const auto& [entrySize, entryAlign] = EntryOf(uniform.format());
      size = entrySize;
      align = entryAlign;

      const size_t offset = alignCursor(align);
      fieldMap[uniform.name()] = {uniform.name(), uniform.format(), offset, size, align};
      cursor = offset + size;
    } else {
      size = uniform.size();
      align = 1;

      const size_t offset = alignCursor(align);
      fieldMap[uniform.name()] = {uniform.name(), uniform.format(), offset, size, align};
      cursor = offset + size;
    }
  }

  bufferSize = alignCursor(_uboSupport ? 16 : 1);
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
