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

#pragma once

#include <list>
#include "gpu/UniformData.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class ProgramWriter;

class Program {
 public:
  Program(std::shared_ptr<RenderPipeline> pipeline, std::unique_ptr<UniformData> vertexUniformData,
          std::unique_ptr<UniformData> fragmentUniformData, std::unique_ptr<ProgramWriter> writer);
  ~Program();

  std::shared_ptr<RenderPipeline> getPipeline() const {
    return pipeline;
  }

  /**
   * The route-specific writer that uploads this program's per-draw uniforms and samplers.
   */
  const ProgramWriter* writer() const {
    return writerValue.get();
  }

  /**
   * The precompiled family the program was built from: lets a cache hit tell which kernel
   * serves a draw without re-running the matcher walk.
   */
  const std::string& shaderName() const {
    return shaderNameValue;
  }

  void setShaderName(std::string name) {
    shaderNameValue = std::move(name);
  }

  UniformData* getUniformData(ShaderStage stage) const;

  /**
   * The number of texture bindings the precompiled artifact declares. Draws providing fewer
   * textures (e.g. an absent device mask) pad the rest with the shared dummy texture.
   */
  uint32_t expectedTextureCount() const {
    return expectedTextureCountValue;
  }

  /**
   * The descriptor binding one texture slot maps to. The assembled Vulkan modules declare their
   * samplers at the variant's own bindings (the leaf index, the mask after the leaves), which are
   * not the slot order the writer walks, so the program carries the mapping; slots without an
   * entry keep their own index (the GL convention, where the two coincide).
   */
  unsigned getTextureBinding(unsigned index) const {
    return index < textureSlotBindings.size() ? textureSlotBindings[index] : index;
  }

  void setTextureSlotBindings(std::vector<unsigned> bindings) {
    textureSlotBindings = std::move(bindings);
  }

  /**
   * How many texture slots the program's descriptor declares overall — the writer binds every
   * one of them (the unused with dummy textures), because statically referenced bindings must be
   * valid at draw even when a branch never runs.
   */
  unsigned textureSlotCount() const {
    return static_cast<unsigned>(textureSlotBindings.size());
  }

  void setExpectedTextureCount(uint32_t count) {
    expectedTextureCountValue = count;
  }

 private:
  BytesKey programKey = {};
  std::list<Program*>::iterator cachedPosition;
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  std::unique_ptr<ProgramWriter> writerValue = nullptr;
  std::string shaderNameValue = {};
  uint32_t expectedTextureCountValue = 0;
  std::vector<unsigned> textureSlotBindings = {};

  friend class GlobalCache;
};
}  // namespace tgfx
