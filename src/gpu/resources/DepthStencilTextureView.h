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

#include "gpu/resources/DefaultTextureView.h"

namespace tgfx {
/**
 * Describes the storage requirements of a depth/stencil attachment texture.
 */
struct DepthStencilSpec {
  int width = 0;
  int height = 0;
  int sampleCount = 1;
  PixelFormat format = PixelFormat::DEPTH24_STENCIL8;
};

/**
 * A cacheable TextureView wrapping a depth/stencil attachment texture. Instances are shared
 * between all render targets with the same DepthStencilSpec through a UniqueKey derived from
 * the spec, so only one physical texture exists per spec at a time.
 */
class DepthStencilTextureView : public DefaultTextureView {
 public:
  size_t memoryUsage() const override;

  /**
   * Creates the depth/stencil texture described by spec and adds it to the cache with both a
   * spec-derived scratch key and the given unique key. Returns nullptr if the texture allocation
   * fails.
   */
  static std::shared_ptr<DepthStencilTextureView> MakeFrom(Context* context,
                                                           const DepthStencilSpec& spec,
                                                           const UniqueKey& uniqueKey);

  /**
   * Derives a stable UniqueKey from the spec. Keys derived from equal specs are always equal, so
   * all render targets with the same spec resolve to the same cached attachment.
   */
  static UniqueKey ComputeSharedAttachmentUniqueKey(const DepthStencilSpec& spec);

  /**
   * Derives the scratch key identifying all attachments with the same spec. Used to find an idle
   * attachment whose unique key has been removed by cache pressure.
   */
  static ScratchKey ComputeDepthStencilScratchKey(const DepthStencilSpec& spec);

  /**
   * Returns the sample count of the depth/stencil texture.
   */
  int sampleCount() const {
    return _sampleCount;
  }

 private:
  int _sampleCount = 1;

  DepthStencilTextureView(std::shared_ptr<Texture> texture, int sampleCount);
};
}  // namespace tgfx
