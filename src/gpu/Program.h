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

enum class ShaderArtifactOrigin : uint8_t {
  RuntimeGeneratedSource,
  OfflineSource,
  OfflineBinary,
};

enum class ProgramOrigin : uint8_t {
  ProgramBuilder,
  PrecompiledArtifact,
};

enum class PipelineOrigin : uint8_t {
  RuntimeCreation,
};

struct ProgramProvenance {
  ShaderArtifactOrigin shaderArtifact = ShaderArtifactOrigin::RuntimeGeneratedSource;
  ProgramOrigin program = ProgramOrigin::ProgramBuilder;
  PipelineOrigin pipeline = PipelineOrigin::RuntimeCreation;
};

class Program {
 public:
  Program(std::shared_ptr<RenderPipeline> pipeline, std::unique_ptr<UniformData> vertexUniformData,
          std::unique_ptr<UniformData> fragmentUniformData, ProgramProvenance provenance);

  std::shared_ptr<RenderPipeline> getPipeline() const {
    return pipeline;
  }

  UniformData* getUniformData(ShaderStage stage) const;

  const ProgramProvenance& getProvenance() const {
    return provenance;
  }

 private:
  BytesKey programKey = {};
  std::list<Program*>::iterator cachedPosition;
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  ProgramProvenance provenance = {};

  friend class GlobalCache;
};
}  // namespace tgfx
