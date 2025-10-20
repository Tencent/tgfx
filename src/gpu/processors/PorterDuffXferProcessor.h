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
  static PlacementPtr<PorterDuffXferProcessor> Make(
      BlockBuffer* buffer, BlendMode blend, DstTextureInfo dstTextureInfo,
      std::shared_ptr<ColorSpace> dstColorSpace = nullptr);

  std::string name() const override {
    return "PorterDuffXferProcessor";
  }

  const TextureView* dstTextureView() const override;

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  PorterDuffXferProcessor(BlendMode blend, DstTextureInfo dstTextureInfo,
                          std::shared_ptr<ColorSpace> colorSpace)
      : XferProcessor(ClassID()), blendMode(blend), dstTextureInfo(std::move(dstTextureInfo)),
        dstColorSpace(std::move(colorSpace)) {
  }

  BlendMode blendMode = BlendMode::SrcOver;
  DstTextureInfo dstTextureInfo = {};
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;
};
}  // namespace tgfx
