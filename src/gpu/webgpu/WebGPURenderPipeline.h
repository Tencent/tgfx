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

#pragma once

#include <webgpu/webgpu_cpp.h>
#include <unordered_map>
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {
class WebGPURenderPipeline : public RenderPipeline {
 public:
  WebGPURenderPipeline(wgpu::RenderPipeline pipeline, wgpu::BindGroupLayout bindGroupLayout,
                       std::unordered_map<unsigned, unsigned> textureBindingMap,
                       size_t vertexStride);

  wgpu::RenderPipeline wgpuPipeline() const {
    return _pipeline;
  }

  wgpu::BindGroupLayout wgpuBindGroupLayout() const {
    return _bindGroupLayout;
  }

  const std::unordered_map<unsigned, unsigned>& textureBindingMap() const {
    return _textureBindingMap;
  }

  size_t vertexStride() const {
    return _vertexStride;
  }

 private:
  wgpu::RenderPipeline _pipeline = nullptr;
  wgpu::BindGroupLayout _bindGroupLayout = nullptr;
  std::unordered_map<unsigned, unsigned> _textureBindingMap = {};
  size_t _vertexStride = 0;
};
}  // namespace tgfx
