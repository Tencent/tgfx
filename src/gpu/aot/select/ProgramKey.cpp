/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties of any kind, either express or
//  implied. see the license for the specific language governing permissions and limitations under
//  the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/ProgramKey.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/processors/GeometryProcessor.h"

namespace tgfx {

BytesKey MakeProgramKey(Context* context, const ProgramInfo& info, uint32_t variantId,
                        const SpecValues& spec) {
  BytesKey key = {};
  key.write(variantId);
  // The values carry no length of their own, so a longer specialization could shift the rest of
  // the key into the words of a shorter one and collide.
  key.write(static_cast<uint32_t>(spec.values.size()));
  spec.writeKey(&key);
  auto* geometryProcessor = info.getGeometryProcessor();
  if (geometryProcessor != nullptr) {
    geometryProcessor->computeProcessorKey(context, &key);
  }
  // The output formula's key contribution: whether the shader runs the xfer stage, and the
  // destination texture when the formula samples one. blendMode already rides the pipeline state
  // key below, and a null proxy means the backend's framebuffer fetch (the third state the old
  // processor's class ID used to encode).
  const auto& xfer = info.getXferFacts();
  key.write(static_cast<uint32_t>(xfer.shaderBlend ? 1u : 0u));
  if (xfer.shaderBlend) {
    const auto& dstProxy = xfer.dstTexture.textureProxy;
    auto dstView = dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
    if (dstView != nullptr) {
      TextureView::ComputeTextureKey(dstView->getTexture(), &key);
    }
  }
  // The blend formula reads this bit (see ProgramInfo::getPipelineColorAttachment), so it is
  // pipeline state even though it rides the processor list.
  key.write(static_cast<uint32_t>(info.blendFormulaHasCoverage() ? 1 : 0));
  info.writePipelineStateKey(&key);
  return key;
}

}  // namespace tgfx
