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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/Xfer.h"
#include <array>
#include "gpu/ProgramInfo.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/shaders/modules/Xfer.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

void WriteXferModule(WriteContext& context) {
  constexpr uint8_t XPBlendMode = UniformElementIndexOf(Xfer, "XPBlendMode");
  constexpr uint8_t DstTextureUpperLeft = UniformElementIndexOf(Xfer, "DstTextureUpperLeft");
  constexpr uint8_t DstTextureCoordScale = UniformElementIndexOf(Xfer, "DstTextureCoordScale");
  constexpr uint8_t OutputAlphaSwizzle = UniformElementIndexOf(Xfer, "OutputAlphaSwizzle");
  // The write swizzle is unconditional (the HAS_XP == 0 path composes too).
  context.slot->set(
      OutputAlphaSwizzle,
      static_cast<int32_t>(context.info.getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0));
  const auto& xfer = context.info.getXferFacts();
  // Only a shader-side formula carries a blend mode and a dst texture; the passthrough
  // (the HAS_XP == 0 variants) declares neither field.
  if (!xfer.shaderBlend()) {
    return;
  }
  // The dst-dialect fields are `when HAS_XP != 0`: absent when the draw's xfer is the
  // passthrough, which the flag above already excludes.
  context.slot->setIfPresent(XPBlendMode, static_cast<int32_t>(context.info.getBlendMode()));
  const auto& dstInfo = *xfer.dstTexture;
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr || dstView->getTexture() == nullptr) {
    return;
  }
  const std::array<float, 2> upperLeft = {dstInfo.offset.x, dstInfo.offset.y};
  context.slot->setIfPresent(DstTextureUpperLeft, upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const std::array<float, 2> scales = {1.f / static_cast<float>(width),
                                       1.f / static_cast<float>(height)};
  context.slot->setIfPresent(DstTextureCoordScale, scales);
}

}  // namespace tgfx
