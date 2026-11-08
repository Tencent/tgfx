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
#include <cstdlib>
#include <cstring>
#include "core/utils/Log.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/UniformData.h"
#include "gpu/shaders/ProgramWriterFactory.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

SpecValues SpecCapture::Capture(SpecSource* source, const ProgramInfo& info,
                                const std::string& shaderName, uint32_t vertIndex,
                                uint32_t fragIndex) {
  SpecValues result;
  if (source == nullptr || !source->specializationEnabled() || !source->isLoaded()) {
    return result;
  }
  auto vertHash = ComputeVertexKeyHash(shaderName, vertIndex, source->profileTag());
  auto fragHash = ComputeFragmentKeyHash(shaderName, fragIndex, source->profileTag());
  auto* specFields = source->findFragmentSpecFields(fragHash.hi, fragHash.lo);
  if (specFields == nullptr) {
    return result;
  }
  auto writer = MakeProgramWriter(shaderName);
  if (writer == nullptr || !writer->canWrite(info)) {
    return result;
  }
  auto* scratch =
      source->findOrCreateSpecScratch(vertHash.hi, vertHash.lo, fragHash.hi, fragHash.lo);
  if (scratch == nullptr) {
    return result;
  }
  if (scratch->fragmentUniformData == nullptr) {
    // Built once per (vertex, fragment) stage pair: the same reflected layouts the created
    // program's uniform data uses, over CPU buffers the writers can fill without a GPU.
    auto* vertUniforms = source->findVertexUniforms(vertHash.hi, vertHash.lo);
    auto* fragUniforms = source->findFragmentUniforms(fragHash.hi, fragHash.lo);
    if (fragUniforms == nullptr) {
      return result;
    }
    if (vertUniforms != nullptr && !vertUniforms->empty()) {
      scratch->vertexUniformData = std::unique_ptr<UniformData>(new UniformData(*vertUniforms));
      scratch->vertexBuffer.resize(scratch->vertexUniformData->size());
    }
    scratch->fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(*fragUniforms));
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
  result.values.reserve(1);
  result.values.push_back(1);  // The master switch: the injected compile is specialized.
  for (const auto& field : *specFields) {
    const auto words = field.arraySize == 1 ? 1u : field.arraySize * 4u;
    for (uint32_t word = 0; word < words; ++word) {
      int32_t value = 0;
      // Read back while the scratch buffer is still attached: the values are read from the very
      // bytes the writers wrote, which is the single-source guarantee.
      if (!scratch->fragmentUniformData->readInt32(field.name, word, &value)) {
        return SpecValues();
      }
      result.values.push_back(value);
    }
  }
  scratch->fragmentUniformData->setBuffer(nullptr);
  if (scratch->vertexUniformData != nullptr) {
    scratch->vertexUniformData->setBuffer(nullptr);
  }
#if DEBUG
  if (std::getenv("TGFX_SPEC_DUMP") != nullptr) {
    std::string text;
    for (auto value : result.values) {
      text += " " + std::to_string(value);
    }
    LOGI("SpecCapture: %s[frag=%u] values:%s", shaderName.c_str(), fragIndex, text.c_str());
  }
#endif
  return result;
}

}  // namespace tgfx
