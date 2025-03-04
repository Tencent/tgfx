/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/Texture.h"
#include "gpu/proxies/GpuBufferProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class GradientCache;

class ResourceProvider {
 public:
  explicit ResourceProvider(Context* context) : context(context) {
  }

  ~ResourceProvider();

  std::shared_ptr<Texture> getGradient(const Color* colors, const float* positions, int count);

  std::shared_ptr<GpuBufferProxy> nonAAQuadIndexBuffer();

  static uint16_t MaxNumNonAAQuads();

  static uint16_t NumIndicesPerNonAAQuad();

  std::shared_ptr<GpuBufferProxy> aaQuadIndexBuffer();

  static uint16_t MaxNumAAQuads();

  static uint16_t NumIndicesPerAAQuad();

  std::shared_ptr<GpuBufferProxy> rRectIndexBuffer();

  static uint16_t MaxNumRRects();

  static uint16_t NumIndicesPerRRect();

  void releaseAll();

 private:
  Context* context = nullptr;
  GradientCache* _gradientCache = nullptr;
  std::shared_ptr<GpuBufferProxy> _aaQuadIndexBuffer = nullptr;
  std::shared_ptr<GpuBufferProxy> _nonAAQuadIndexBuffer = nullptr;
  std::shared_ptr<GpuBufferProxy> _rRectIndexBuffer = nullptr;
};
}  // namespace tgfx
