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
#include "gpu/UniformLayout.h"

namespace tgfx {

UniformData::UniformData(std::vector<Uniform> uniforms) : _uniforms(std::move(uniforms)) {
  for (const auto& uniform : _uniforms) {
    const auto layout = Std140LayoutOf(uniform.format());
    if (layout.size == 0) {
      // A sampler (or an unknown format) is a binding, not a member of the block.
      continue;
    }
    const auto count = uniform.count();
    const size_t size = count > 1 ? static_cast<size_t>(layout.arrayStride) * count : layout.size;
    const size_t offset = alignCursor(layout.alignment);
    fieldMap[uniform.name()] = {uniform.name(), uniform.format(), offset, size, layout.alignment};
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
    std::string known;
    for (const auto& [fieldKey, entry] : fieldMap) {
      if (!known.empty()) {
        known += ",";
      }
      known += fieldKey;
    }
    LOGE("UniformData::onSetData() uniform '%s' (suffix '%s') not found! known: %s", name.c_str(),
         nameSuffix.c_str(), known.c_str());
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
  return Std140AlignUp(static_cast<uint32_t>(cursor), static_cast<uint32_t>(alignment));
}

}  // namespace tgfx
