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

#include "gpu/BlendFormula.h"
#include "gpu/processors/XferProcessor.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {
class PorterDuffXferProcessor : public XferProcessor {
 public:
  DEFINE_PROCESSOR_CLASS_ID
  static PlacementPtr<PorterDuffXferProcessor> Make(BlockAllocator* allocator, BlendMode blend,
                                                    DstTextureInfo dstTextureInfo);

  std::string name() const override {
    return "PorterDuffXferProcessor";
  }

  BlendMode getBlendMode() const {
    return blendMode;
  }

  /** The destination-texture fetch state the precompiled XP uniforms carry (offset into the
   * shared dst texture); a null proxy means no framebuffer fetch for this draw. */
  const DstTextureInfo& getDstTextureInfo() const {
    return dstTextureInfo;
  }

  const TextureView* dstTextureView() const override;

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

 protected:
  PorterDuffXferProcessor(BlendMode blend, DstTextureInfo dstTextureInfo)
      : XferProcessor(ClassID()), blendMode(blend), dstTextureInfo(std::move(dstTextureInfo)) {
  }

  BlendMode blendMode = BlendMode::SrcOver;
  DstTextureInfo dstTextureInfo = {};
};
}  // namespace tgfx
