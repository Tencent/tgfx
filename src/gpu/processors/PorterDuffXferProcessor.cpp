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

#include "PorterDuffXferProcessor.h"

namespace tgfx {

void PorterDuffXferProcessor::computeProcessorKey(Context*, BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(blendFormula.primaryOutputType()) |
                  (static_cast<uint32_t>(blendFormula.secondaryOutputType()) << 3) |
                  (static_cast<uint32_t>(blendFormula.equation()) << 6) |
                  (static_cast<uint32_t>(blendFormula.srcCoeff()) << 8) |
                  (static_cast<uint32_t>(blendFormula.dstCoeff()) << 12));
}

const Texture* PorterDuffXferProcessor::dstTexture() const {
  auto textureProxy = dstTextureInfo.textureProxy;
  return textureProxy ? textureProxy->getTexture().get() : nullptr;
}

}  // namespace tgfx
