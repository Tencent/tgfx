/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "ShaderCache.h"
#include <cstdlib>
#include "core/utils/Log.h"
#include "core/utils/MD5.h"
#include "tgfx/core/Clock.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Bumped whenever EncodeShaderModuleDescriptor changes, so keys from different encodings never
// compare equal (matters once keys are persisted).
static constexpr uint8_t ModuleKeyVersion = 1;

static void AppendU32(std::string* out, uint32_t value) {
  for (int i = 0; i < 4; i++) {
    out->push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
  }
}

static void AppendBytes(std::string* out, const void* data, size_t size) {
  AppendU32(out, static_cast<uint32_t>(size));
  out->append(static_cast<const char*>(data), size);
}

void EncodeShaderModuleDescriptor(const ShaderModuleDescriptor& descriptor, std::string* out) {
  out->clear();
  out->reserve(descriptor.code.size() + descriptor.binaryData.size() + 64);
  out->push_back(static_cast<char>(ModuleKeyVersion));
  AppendU32(out, static_cast<uint32_t>(descriptor.format));
  AppendU32(out, static_cast<uint32_t>(descriptor.stage));
  AppendBytes(out, descriptor.code.data(), descriptor.code.size());
  AppendBytes(out, descriptor.binaryData.data(), descriptor.binaryData.size());
  // std::map iterates in name order, so the encoding does not depend on insertion order.
  AppendU32(out, static_cast<uint32_t>(descriptor.uniformSlots.size()));
  for (const auto& [name, slot] : descriptor.uniformSlots) {
    AppendBytes(out, name.data(), name.size());
    AppendU32(out, slot);
  }
}

ShaderModuleKey MakeShaderModuleKey(const std::string& encoded) {
  ShaderModuleKey key;
  key.digest = MD5::Calculate(encoded.data(), encoded.size());
  key.encodedSize = encoded.size();
  return key;
}

static bool DisabledByEnvironment(const char* layer) {
  const char* value = std::getenv("TGFX_SHADER_CACHE_DISABLE");
  if (value == nullptr) {
    return false;
  }
  std::string list = value;
  return list.find("all") != std::string::npos || list.find(layer) != std::string::npos;
}

ShaderCache::ShaderCache(Context* context)
    : context(context), moduleCacheEnabled(!DisabledByEnvironment("L1")) {
}

std::shared_ptr<ShaderModule> ShaderCache::createModule(const ShaderModuleDescriptor& descriptor) {
  auto start = Clock::Now();
  auto module = context->gpu()->createShaderModule(descriptor);
  _stats.moduleCreationMicros += Clock::Now() - start;
  if (module == nullptr) {
    _stats.moduleCreationFailures++;
  } else {
    _stats.moduleCreations++;
  }
  return module;
}

std::shared_ptr<ShaderModule> ShaderCache::findOrCreateModule(
    const ShaderModuleDescriptor& descriptor) {
  _stats.moduleRequests++;
  if (!moduleCacheEnabled) {
    return createModule(descriptor);
  }
  std::string encoded;
  EncodeShaderModuleDescriptor(descriptor, &encoded);
  auto key = MakeShaderModuleKey(encoded);
  auto found = moduleMap.find(key);
  if (found != moduleMap.end()) {
    auto& entry = found->second;
#ifdef DEBUG
    if (entry.encoded != encoded) {
      LOGE("ShaderCache: shader module key collision; compiling the module uncached.");
      return createModule(descriptor);
    }
#endif
    moduleLRU.splice(moduleLRU.begin(), moduleLRU, entry.lruPosition);
    _stats.moduleHits++;
    return entry.module;
  }
  auto module = createModule(descriptor);
  if (module == nullptr) {
    return nullptr;
  }
  moduleLRU.push_front(key);
  ModuleEntry entry;
  entry.module = module;
  entry.lruPosition = moduleLRU.begin();
#ifdef DEBUG
  entry.encoded = std::move(encoded);
#endif
  moduleMap.emplace(key, std::move(entry));
  while (moduleLRU.size() > MaxModuleCount) {
    moduleMap.erase(moduleLRU.back());
    moduleLRU.pop_back();
    _stats.moduleEvictions++;
  }
  return module;
}

void ShaderCache::clear() {
  moduleMap.clear();
  moduleLRU.clear();
}

}  // namespace tgfx
