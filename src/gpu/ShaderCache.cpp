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

// Bumped whenever the corresponding encoding changes, so keys from different encodings never
// compare equal (matters once keys are persisted). The two versions differ so a module key and a
// pipeline key can never collide either.
static constexpr uint8_t ModuleKeyVersion = 1;
static constexpr uint8_t PipelineKeyVersion = 129;

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

static void AppendString(std::string* out, const std::string& text) {
  AppendBytes(out, text.data(), text.size());
}

static bool AppendModule(std::string* out, const std::shared_ptr<ShaderModule>& module,
                         const std::function<const ShaderModuleKey*(const ShaderModule*)>& keyOf) {
  if (module == nullptr) {
    out->push_back(0);
    return true;
  }
  auto key = keyOf(module.get());
  if (key == nullptr) {
    return false;
  }
  out->push_back(1);
  out->append(reinterpret_cast<const char*>(key->digest.data()), key->digest.size());
  AppendU32(out, static_cast<uint32_t>(key->encodedSize));
  AppendU32(out, static_cast<uint32_t>(key->encodedSize >> 32));
  return true;
}

static void AppendStencil(std::string* out, const StencilDescriptor& stencil) {
  AppendU32(out, static_cast<uint32_t>(stencil.compare));
  AppendU32(out, static_cast<uint32_t>(stencil.depthFailOp));
  AppendU32(out, static_cast<uint32_t>(stencil.failOp));
  AppendU32(out, static_cast<uint32_t>(stencil.passOp));
}

static void AppendBindings(std::string* out, const std::vector<BindingEntry>& entries) {
  AppendU32(out, static_cast<uint32_t>(entries.size()));
  for (const auto& entry : entries) {
    AppendString(out, entry.name);
    AppendU32(out, entry.binding);
    AppendU32(out, entry.visibility);
  }
}

bool EncodeRenderPipelineDescriptor(
    const RenderPipelineDescriptor& descriptor,
    const std::function<const ShaderModuleKey*(const ShaderModule*)>& keyOf, std::string* out) {
  out->clear();
  out->push_back(static_cast<char>(PipelineKeyVersion));
  // Vertex stage.
  const auto& vertex = descriptor.vertex;
  if (!AppendModule(out, vertex.module, keyOf)) {
    return false;
  }
  AppendString(out, vertex.entryPoint);
  AppendU32(out, vertex.bindAttributesByLocation ? 1 : 0);
  AppendU32(out, static_cast<uint32_t>(vertex.bufferLayouts.size()));
  for (const auto& layout : vertex.bufferLayouts) {
    AppendU32(out, static_cast<uint32_t>(layout.stride));
    AppendU32(out, static_cast<uint32_t>(layout.stepMode));
    AppendU32(out, static_cast<uint32_t>(layout.attributes.size()));
    for (const auto& attribute : layout.attributes) {
      AppendString(out, attribute.name());
      AppendU32(out, static_cast<uint32_t>(attribute.format()));
    }
  }
  // Fragment stage.
  const auto& fragment = descriptor.fragment;
  if (!AppendModule(out, fragment.module, keyOf)) {
    return false;
  }
  AppendString(out, fragment.entryPoint);
  AppendU32(out, static_cast<uint32_t>(fragment.colorAttachments.size()));
  for (const auto& color : fragment.colorAttachments) {
    AppendU32(out, static_cast<uint32_t>(color.format));
    AppendU32(out, color.blendEnable ? 1 : 0);
    AppendU32(out, static_cast<uint32_t>(color.srcColorBlendFactor));
    AppendU32(out, static_cast<uint32_t>(color.dstColorBlendFactor));
    AppendU32(out, static_cast<uint32_t>(color.colorBlendOp));
    AppendU32(out, static_cast<uint32_t>(color.srcAlphaBlendFactor));
    AppendU32(out, static_cast<uint32_t>(color.dstAlphaBlendFactor));
    AppendU32(out, static_cast<uint32_t>(color.alphaBlendOp));
    AppendU32(out, color.colorWriteMask);
  }
  // Resource layout.
  AppendBindings(out, descriptor.layout.uniformBlocks);
  AppendBindings(out, descriptor.layout.textureSamplers);
  // Fixed-function state.
  const auto& depthStencil = descriptor.depthStencil;
  AppendU32(out, static_cast<uint32_t>(depthStencil.depthCompare));
  AppendU32(out, depthStencil.depthWriteEnabled ? 1 : 0);
  AppendStencil(out, depthStencil.stencilBack);
  AppendStencil(out, depthStencil.stencilFront);
  AppendU32(out, depthStencil.stencilReadMask);
  AppendU32(out, depthStencil.stencilWriteMask);
  AppendU32(out, static_cast<uint32_t>(depthStencil.format));
  AppendU32(out, static_cast<uint32_t>(descriptor.primitive.cullMode));
  AppendU32(out, static_cast<uint32_t>(descriptor.primitive.frontFace));
  AppendU32(out, static_cast<uint32_t>(descriptor.multisample.count));
  AppendU32(out, descriptor.multisample.mask);
  AppendU32(out, descriptor.multisample.alphaToCoverageEnabled ? 1 : 0);
  return true;
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
    : context(context), moduleCacheEnabled(!DisabledByEnvironment("L1")),
      pipelineCacheEnabled(moduleCacheEnabled && !DisabledByEnvironment("L2")) {
}

const ShaderModuleKey* ShaderCache::findModuleKey(const ShaderModule* module) const {
  auto found = moduleKeys.find(module);
  return found != moduleKeys.end() ? &found->second : nullptr;
}

std::shared_ptr<RenderPipeline> ShaderCache::createPipeline(
    const RenderPipelineDescriptor& descriptor) {
  auto start = Clock::Now();
  auto pipeline = context->gpu()->createRenderPipeline(descriptor);
  _stats.pipelineCreationMicros += Clock::Now() - start;
  if (pipeline == nullptr) {
    _stats.pipelineCreationFailures++;
  } else {
    _stats.pipelineCreations++;
  }
  return pipeline;
}

std::shared_ptr<RenderPipeline> ShaderCache::findOrCreatePipeline(
    const RenderPipelineDescriptor& descriptor, bool* created) {
  _stats.pipelineRequests++;
  *created = true;
  if (!pipelineCacheEnabled) {
    return createPipeline(descriptor);
  }
  std::string encoded;
  auto keyOf = [this](const ShaderModule* module) { return findModuleKey(module); };
  if (!EncodeRenderPipelineDescriptor(descriptor, keyOf, &encoded)) {
    _stats.pipelineUncacheable++;
    return createPipeline(descriptor);
  }
  auto key = MakeShaderModuleKey(encoded);
  auto found = pipelineMap.find(key);
  if (found != pipelineMap.end()) {
    auto& entry = found->second;
#ifdef DEBUG
    if (entry.encoded != encoded) {
      LOGE("ShaderCache: render pipeline key collision; creating the pipeline uncached.");
      return createPipeline(descriptor);
    }
#endif
    pipelineLRU.splice(pipelineLRU.begin(), pipelineLRU, entry.lruPosition);
    _stats.pipelineHits++;
    *created = false;
    return entry.pipeline;
  }
  auto pipeline = createPipeline(descriptor);
  if (pipeline == nullptr) {
    return nullptr;
  }
  pipelineLRU.push_front(key);
  PipelineEntry entry;
  entry.pipeline = pipeline;
  entry.lruPosition = pipelineLRU.begin();
#ifdef DEBUG
  entry.encoded = std::move(encoded);
#endif
  pipelineMap.emplace(key, std::move(entry));
  while (pipelineLRU.size() > MaxPipelineCount) {
    pipelineMap.erase(pipelineLRU.back());
    pipelineLRU.pop_back();
    _stats.pipelineEvictions++;
  }
  return pipeline;
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
  moduleKeys[module.get()] = key;
  moduleMap.emplace(key, std::move(entry));
  while (moduleLRU.size() > MaxModuleCount) {
    auto evicted = moduleMap.find(moduleLRU.back());
    moduleKeys.erase(evicted->second.module.get());
    moduleMap.erase(evicted);
    moduleLRU.pop_back();
    _stats.moduleEvictions++;
  }
  return module;
}

void ShaderCache::clear() {
  pipelineMap.clear();
  pipelineLRU.clear();
  moduleKeys.clear();
  moduleMap.clear();
  moduleLRU.clear();
}

}  // namespace tgfx
