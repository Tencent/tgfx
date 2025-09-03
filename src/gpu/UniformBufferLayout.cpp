/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "UniformBufferLayout.h"
#include <algorithm>
#include <vector>
#include "core/utils/Log.h"

namespace tgfx {
size_t UniformBufferLayout::add(const Uniform& uniform, size_t baseOffset) {
  size_t size = 0;
  size_t align = 0;
  if (_uboSupport) {
    const auto& [entrySize, entryAlign] = EntryOf(uniform.format());
    size = entrySize;
    align = entryAlign;

    const size_t offset = alignCursor(align);
    fieldMap[uniform.name()] = {uniform.name(), uniform.format(), baseOffset + offset, size, align};
    cursor = offset + size;
    return offset;
  } else {
    size = uniform.size();
    align = 1;

    const size_t offset = alignCursor(align);
    fieldMap[uniform.name()] = {uniform.name(), uniform.format(), baseOffset + offset, size, align};
    cursor = offset + size;
    return offset;
  }
}

size_t UniformBufferLayout::totalSize() const {
  return alignCursor(_uboSupport ? 16 : 1);
}

const UniformBufferLayout::Field* UniformBufferLayout::findField(const std::string& key) const {
  const auto& iter = fieldMap.find(key);
  if (iter != fieldMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

#if DEBUG
void UniformBufferLayout::dump() const {
  LOGI("\n-------------- Std140Layout dump begin --------------");
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
  LOGI("Total buffer size = %zu bytes", totalSize());
  LOGI("-------------- Std140Layout dump end --------------\n");
}
#endif

UniformBufferLayout::Entry UniformBufferLayout::EntryOf(UniformFormat format) {
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

size_t UniformBufferLayout::alignCursor(size_t alignment) const {
  return (cursor + alignment - 1) / alignment * alignment;
}
}  // namespace tgfx
