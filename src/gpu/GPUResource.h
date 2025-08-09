/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

namespace tgfx {
class GPU;

/**
 * GPUResource is the base class for GPU resources that need manual release of their underlying
 * allocations. It is intended for resources like buffers and textures that are not automatically
 * managed by the GPU. This class decouples resource release from object destruction, allowing you
 * to skip releasing resources in scenarios such as when the GPU device is lost or destroyed.
 * Otherwise, it may lead to undefined behavior.
 */
class GPUResource {
 public:
  virtual ~GPUResource() = default;

  /**
   * Releases the underlying GPU resources. After calling this method, the GPUResource must not be
   * used, as doing so may lead to undefined behavior.
   */
  virtual void release(GPU* gpu) = 0;
};
}  // namespace tgfx