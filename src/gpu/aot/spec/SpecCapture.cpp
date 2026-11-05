/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecCapture.h"
#include <cstring>
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/UniformData.h"
#include "gpu/aot/bundle/PrecompiledProgramCreator.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

SpecValues SpecCapture::Capture(Context* context, const ProgramInfo& info,
                                const std::string& shaderName, uint32_t vertIndex,
                                uint32_t fragIndex) {
  SpecValues result;
  if (context == nullptr) {
    return result;
  }
  auto cache = context->precompiledShaderCache();
  if (cache == nullptr || !cache->specializationEnabled() || !cache->isLoaded()) {
    return result;
  }
  auto vertHash = ComputeVertexKeyHash(shaderName, vertIndex, cache->profileTag());
  auto fragHash = ComputeFragmentKeyHash(shaderName, fragIndex, cache->profileTag());
  auto fragBlob = cache->findFragment(fragHash.hi, fragHash.lo);
  if (fragBlob == nullptr || fragBlob->specFields.empty()) {
    return result;
  }
  auto writer = MakeProgramWriter(shaderName);
  if (writer == nullptr || !writer->canWrite(info)) {
    return result;
  }
  auto* scratch =
      cache->findOrCreateSpecScratch(vertHash.hi, vertHash.lo, fragHash.hi, fragHash.lo);
  if (scratch == nullptr) {
    return result;
  }
  if (scratch->fragmentUniformData == nullptr) {
    // Built once per (vertex, fragment) stage pair: the same reflected layouts the created
    // program's uniform data uses, over CPU buffers the writers can fill without a GPU.
    auto vertBlob = cache->findVertex(vertHash.hi, vertHash.lo);
    if (vertBlob != nullptr && !vertBlob->uniforms.empty()) {
      scratch->vertexUniformData =
          std::unique_ptr<UniformData>(new UniformData(vertBlob->uniforms));
      scratch->vertexUniformData->skipSuffix = true;
      scratch->vertexBuffer.resize(scratch->vertexUniformData->size());
    }
    scratch->fragmentUniformData =
        std::unique_ptr<UniformData>(new UniformData(fragBlob->uniforms));
    scratch->fragmentUniformData->skipSuffix = true;
    scratch->fragmentBuffer.resize(scratch->fragmentUniformData->size());
  }
  // Zeroed before every capture: a recycled buffer would leak an earlier draw's structural words
  // into this draw's key, and the untouched words are deterministic zero (an out-of-range slot's
  // injected constant is never read — the slot-count guard folds the call site away first).
  if (!scratch->fragmentBuffer.empty()) {
    std::memset(scratch->fragmentBuffer.data(), 0, scratch->fragmentBuffer.size());
  }
  if (!scratch->vertexBuffer.empty()) {
    std::memset(scratch->vertexBuffer.data(), 0, scratch->vertexBuffer.size());
  }
  if (scratch->vertexUniformData != nullptr) {
    scratch->vertexUniformData->setBuffer(scratch->vertexBuffer.data());
  }
  scratch->fragmentUniformData->setBuffer(scratch->fragmentBuffer.data());
  writer->writeUniformValues(info, scratch->vertexUniformData.get(),
                             scratch->fragmentUniformData.get());
  scratch->fragmentUniformData->setBuffer(nullptr);
  if (scratch->vertexUniformData != nullptr) {
    scratch->vertexUniformData->setBuffer(nullptr);
  }
  result.values.reserve(1);
  result.values.push_back(1);  // The master switch: the injected compile is specialized.
  for (const auto& field : fragBlob->specFields) {
    const auto words = field.arraySize == 1 ? 1u : field.arraySize * 4u;
    for (uint32_t word = 0; word < words; ++word) {
      int32_t value = 0;
      if (!scratch->fragmentUniformData->readInt32(field.name, word, &value)) {
        return SpecValues();
      }
      result.values.push_back(value);
    }
  }
  return result;
}

}  // namespace tgfx
