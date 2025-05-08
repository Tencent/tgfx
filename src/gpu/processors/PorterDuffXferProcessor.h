/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/Blend.h"
#include "gpu/processors/XferProcessor.h"

namespace tgfx {
class PorterDuffXferProcessor : public XferProcessor {
 public:
  static PlacementPtr<PorterDuffXferProcessor> Make(BlockBuffer* buffer,
                                                    const BlendFormula& formula,
                                                    const DstTextureInfo& dstTextureInfo);

  std::string name() const override {
    return "PorterDuffXferProcessor";
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

  const Texture* dstTexture() const override;

  bool requiresBarrier() const override {
    return dstTextureInfo.requiresBarrier;
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit PorterDuffXferProcessor(const BlendFormula& blendFormula,
                                   const DstTextureInfo& dstTextureInfo)
      : XferProcessor(ClassID()), blendFormula(blendFormula), dstTextureInfo(dstTextureInfo) {
  }

  BlendFormula blendFormula = {};

  DstTextureInfo dstTextureInfo = {};
};
}  // namespace tgfx
