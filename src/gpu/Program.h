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
#include "gpu/shaders/modules/ParamHandle.h"
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

  friend class GlobalCache;
};
}  // namespace tgfx
