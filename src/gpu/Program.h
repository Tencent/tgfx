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

#pragma once

#include <cstdint>
#include <list>
#include "gpu/UniformData.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class Program {
 public:
  Program(std::shared_ptr<RenderPipeline> pipeline, std::unique_ptr<UniformData> vertexUniformData,
          std::unique_ptr<UniformData> fragmentUniformData, uint64_t sourceDigest = 0);
  ~Program();

  std::shared_ptr<RenderPipeline> getPipeline() const {
    return pipeline;
  }

  UniformData* getUniformData(ShaderStage stage) const;

  /// The digest of the shader source this program was built from. Zero when the backend does not
  /// supply one. The Debug program-key audit compares it between a cache hit and a rebuild.
  uint64_t sourceDigest() const {
    return _sourceDigest;
  }

 private:
  BytesKey programKey = {};
  std::list<Program*>::iterator cachedPosition;
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  uint64_t _sourceDigest = 0;

  friend class GlobalCache;
};
}  // namespace tgfx
