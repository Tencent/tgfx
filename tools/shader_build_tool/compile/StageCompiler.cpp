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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "StageCompiler.h"

#include <cstring>
#include <utility>

namespace tgfx {

namespace {

// The Metal compiler's version and the macOS SDK it targets, queried once. Part of every metallib
// key: a new Xcode can compile the same MSL to different bytes.
const std::string& MetalToolchainKey() {
  static const std::string key = MetalCompilerFingerprint() + "\n" + MetalToolchainVersion();
  return key;
}

}  // namespace

StageCompiler::StageCompiler(StageCache* persistentCache) : persistentCache(persistentCache) {}

const SpirvStage& StageCompiler::compileSpirv(const std::string& source, ShaderStageType stage,
                                              const std::string& shaderName,
                                              uint32_t variantIndex, bool optimize) {
  const char* kind = optimize ? "spirv-optimized" : "spirv";
  auto& stats = persistentCache->stats(kind);
  stats.requests++;
  // shaderc receives shaderName_variant_N as the input file name, so it is part of the input.
  auto key = StageCacheKey(kind, persistentCache->toolIdentity())
                 .add(shaderName + "_variant_" + std::to_string(variantIndex))
                 .add(static_cast<uint64_t>(stage))
                 .add(static_cast<uint64_t>(optimize))
                 .add(source)
                 .finish();
  auto it = spirvCache.find(key);
  if (it != spirvCache.end()) {
    stats.memoryHits++;
    return it->second;
  }
  SpirvStage entry;
  std::vector<uint8_t> payload;
  if (persistentCache->load(key, &payload, &stats) && payload.size() % 4 == 0) {
    entry.success = true;
    entry.spirv.resize(payload.size() / 4);
    std::memcpy(entry.spirv.data(), payload.data(), payload.size());
  } else {
    stats.executed++;
    entry.spirv = CompileGLSL(source, stage, shaderName, variantIndex, optimize, &entry.error);
    entry.success = !entry.spirv.empty();
    if (entry.success) {
      auto* bytes = reinterpret_cast<const uint8_t*>(entry.spirv.data());
      persistentCache->store(key,
                             std::vector<uint8_t>(bytes, bytes + entry.spirv.size() * 4), &stats);
    }
  }
  return spirvCache.emplace(std::move(key), std::move(entry)).first->second;
}

std::vector<uint8_t> StageCompiler::compileMetallib(const std::string& msl, ShaderStageType stage,
                                                    std::string* error) {
  auto key = StageCacheKey("metallib", persistentCache->toolIdentity())
                 .add(MetalToolchainKey())
                 .add(static_cast<uint64_t>(stage))
                 .add(msl)
                 .finish();
  auto& stats = persistentCache->stats("metallib");
  stats.requests++;
  auto it = blobCache.find(key);
  if (it == blobCache.end()) {
    BlobStage entry;
    if (persistentCache->load(key, &entry.bytes, &stats)) {
      entry.success = true;
    } else {
      stats.executed++;
      entry.bytes = CompileMSLToMetallib(msl, stage, &entry.error);
      entry.success = !entry.bytes.empty();
      if (entry.success) {
        persistentCache->store(key, entry.bytes, &stats);
      }
    }
    it = blobCache.emplace(std::move(key), std::move(entry)).first;
  } else {
    stats.memoryHits++;
  }
  if (!it->second.success) {
    *error = it->second.error;
    return {};
  }
  return it->second.bytes;
}

const TextStage& StageCompiler::compileWgsl(const std::string& source, ShaderStageType stage,
                                            const std::string& shaderName,
                                            uint32_t variantIndex) {
  auto key = StageCacheKey("wgsl", persistentCache->toolIdentity())
                 .add(shaderName + "_variant_" + std::to_string(variantIndex))
                 .add(static_cast<uint64_t>(stage))
                 .add(source)
                 .finish();
  auto& stats = persistentCache->stats("wgsl");
  stats.requests++;
  auto it = wgslCache.find(key);
  if (it == wgslCache.end()) {
    TextStage entry;
    std::vector<uint8_t> payload;
    if (persistentCache->load(key, &payload, &stats)) {
      entry.success = true;
      entry.text.assign(payload.begin(), payload.end());
    } else {
      stats.executed++;
      entry.text = CompileGLSLToWGSL(source, stage, shaderName, variantIndex, &entry.error);
      entry.success = !entry.text.empty();
      if (entry.success) {
        persistentCache->store(key,
                               std::vector<uint8_t>(entry.text.begin(), entry.text.end()), &stats);
      }
    }
    it = wgslCache.emplace(std::move(key), std::move(entry)).first;
  } else {
    stats.memoryHits++;
  }
  return it->second;
}

const ReflectionResult& StageCompiler::extractReflection(const std::string& shaderName,
                                                         uint32_t vertIndex, uint32_t fragIndex,
                                                         const std::vector<uint32_t>& vertSpirv,
                                                         const std::vector<uint32_t>& fragSpirv,
                                                         Diagnostics* diagnostics) {
  auto key = std::make_tuple(shaderName, vertIndex, fragIndex);
  auto it = reflectionCache.find(key);
  if (it == reflectionCache.end()) {
    it = reflectionCache
             .emplace(std::move(key), ExtractReflection(vertSpirv, fragSpirv, diagnostics))
             .first;
  }
  return it->second;
}

}  // namespace tgfx
