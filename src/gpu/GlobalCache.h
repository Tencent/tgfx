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
#include <unordered_map>
#include "gpu/Program.h"
#include "gpu/ProgramCreator.h"
#include "gpu/proxies/GpuBufferProxy.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
/**
 * GlobalCache manages GPU resources that need to stay alive for the lifetime of the Context.
 */
class GlobalCache {
 public:
  explicit GlobalCache(Context* context);

  /**
   * Returns a program cache of specified ProgramMaker. If there is no associated cache available,
   * a new program will be created by programMaker. Returns null if the programMaker fails to make a
   * new program.
   */
  std::shared_ptr<Program> getProgram(const ProgramCreator* programCreator);

  /**
   * Returns a texture that represents a gradient created from the specified colors and positions.
   */
  std::shared_ptr<TextureProxy> getGradient(const Color* colors, const float* positions, int count);

  /**
   * Returns a GPU buffer that contains indices for rendering a quad with or without antialiasing.
   */
  std::shared_ptr<GpuBufferProxy> getRectIndexBuffer(bool antialias);

  /**
   * Returns a GPU buffer containing indices for rendering a rounded rectangle, either for filling
   * or stroking.
   */
  std::shared_ptr<GpuBufferProxy> getRRectIndexBuffer(bool stroke);

 private:
  Context* context = nullptr;
  std::list<Program*> programLRU = {};
  BytesKeyMap<std::shared_ptr<Program>> programMap = {};
  std::list<BytesKey> gradientKeys = {};
  BytesKeyMap<std::shared_ptr<TextureProxy>> gradientTextures = {};
  std::shared_ptr<GpuBufferProxy> aaQuadIndexBuffer = nullptr;
  std::shared_ptr<GpuBufferProxy> nonAAQuadIndexBuffer = nullptr;
  std::shared_ptr<GpuBufferProxy> rRectFillIndexBuffer = nullptr;
  std::shared_ptr<GpuBufferProxy> rRectStrokeIndexBuffer = nullptr;

  void releaseAll();

  friend class Context;
};
}  // namespace tgfx
