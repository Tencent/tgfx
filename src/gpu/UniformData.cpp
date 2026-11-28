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

#include "UniformData.h"
#include <cstring>
#include "core/utils/Log.h"
#include "gpu/ir/ProgramIR.h"

namespace tgfx {

std::unique_ptr<UniformData> UniformData::Make(const InterfaceIR& interface, ShaderStage stage) {
  const UniformBlockIR* block = nullptr;
  for (const auto& candidate : interface.uniformBlocks) {
    if (candidate.stage != stage) {
      continue;
    }
    if (block != nullptr) {
      // More than one block per stage needs a per-block name mapping the upload does not have;
      // reject instead of silently picking the first.
      LOGE("UniformData::Make(): the stage declares more than one uniform block");
      return nullptr;
    }
    block = &candidate;
  }
  if (block == nullptr) {
    return nullptr;
  }
  auto data = std::unique_ptr<UniformData>(new UniformData());
  data->bufferSize = block->size;
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage != stage || uniform.size == 0) {
      continue;
    }
    DEBUG_ASSERT(uniform.offset + uniform.size <= block->size);
    data->fieldMap[uniform.name] = {uniform.offset, uniform.size};
  }
  return data;
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

}  // namespace tgfx
