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
class Program {
 public:
  Program(std::shared_ptr<ShaderModule> vertexShader, std::shared_ptr<ShaderModule> fragmentShader,
          BindingLayout bindingLayout, std::unique_ptr<UniformData> vertexUniformData,
          std::unique_ptr<UniformData> fragmentUniformData);

  std::shared_ptr<ShaderModule> getVertexShader() const {
    return vertexShader;
  }

  std::shared_ptr<ShaderModule> getFragmentShader() const {
    return fragmentShader;
  }

  const BindingLayout& getBindingLayout() const {
    return bindingLayout;
  }

  UniformData* getUniformData(ShaderStage stage) const;

 private:
  BytesKey programKey = {};
  std::list<Program*>::iterator cachedPosition;
  std::shared_ptr<ShaderModule> vertexShader = nullptr;
  std::shared_ptr<ShaderModule> fragmentShader = nullptr;
  BindingLayout bindingLayout = {};
  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;

  friend class GlobalCache;
};
}  // namespace tgfx
