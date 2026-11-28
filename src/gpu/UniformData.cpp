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
    // Several processors may declare the same base name; the owner tells their entries apart.
    data->byBaseName[uniform.baseName].push_back(
        {uniform.name, uniform.owner, uniform.offset, uniform.size});
  }
  return data;
}

void UniformData::setBuffer(void* buffer) {
  _buffer = static_cast<uint8_t*>(buffer);
}

void UniformData::setOwner(uint32_t newOwner) {
  owner = newOwner;
}

bool UniformData::hasUploadError() const {
  return uploadError;
}

size_t UniformData::unwrittenCount() const {
  size_t count = 0;
  for (const auto& [baseName, fields] : byBaseName) {
    for (const auto& field : fields) {
      if (!field.written) {
        ++count;
      }
    }
  }
  return count;
}

std::vector<std::string> UniformData::unwrittenNames() const {
  std::vector<std::string> names = {};
  for (const auto& [baseName, fields] : byBaseName) {
    for (const auto& field : fields) {
      if (!field.written) {
        names.push_back(field.name);
      }
    }
  }
  return names;
}

void UniformData::onSetData(const std::string& baseName, const void* data, size_t size) const {
  DEBUG_ASSERT(_buffer != nullptr);

  const auto* field = findField(baseName);
  if (field == nullptr) {
    std::string known;
    for (const auto& [name, fields] : byBaseName) {
      if (!known.empty()) {
        known += ",";
      }
      known += name;
    }
    LOGE("UniformData::onSetData() uniform '%s' has no member owned by %u! known: %s",
         baseName.c_str(), owner, known.c_str());
    uploadError = true;
    return;
  }
  if (size > field->size) {
    // The old path asserted in Debug and memcpy'd past the block in Release; a hard error here is
    // the only safe behavior, because the destination is a mapped UBO.
    LOGE("UniformData::onSetData() uniform '%s' wrote %zu bytes into %zu", baseName.c_str(), size,
         field->size);
    uploadError = true;
    return;
  }
  memcpy(_buffer + field->offset, data, size);
  field->written = true;
}

const UniformData::Field* UniformData::findField(const std::string& baseName) const {
  const auto iter = byBaseName.find(baseName);
  if (iter == byBaseName.end()) {
    return nullptr;
  }
  for (const auto& field : iter->second) {
    if (field.owner == owner) {
      return &field;
    }
  }
  return nullptr;
}

}  // namespace tgfx
