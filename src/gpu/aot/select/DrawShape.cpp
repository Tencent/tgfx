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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ProgramInfo.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

namespace {

void AddReadSwizzle(const std::shared_ptr<Texture>& texture, uint32_t* required) {
  if (texture != nullptr && AOTReadSwizzleMode(texture->format()) != 0) {
    *required |= ShaderTextureReadSwizzle;
  }
}

// The ShaderTextureRead semantics the draw's sampled textures need. YUV planes are excluded:
// the YUV kernels read the plane channels they need (.r / .rg), which the read swizzle preserves.
uint32_t RequiredTextureReads(const ProgramInfo* programInfo) {
  uint32_t required = 0;
  // The geometry's own texture (the glyph atlas) is the only geometry sampler.
  const auto& atlasProxy = programInfo->getGeometryLayout().parameters.atlasProxy;
  if (atlasProxy != nullptr) {
    auto atlasView = atlasProxy->getTextureView();
    if (atlasView != nullptr) {
      AddReadSwizzle(atlasView->getTexture(), &required);
    }
  }
  const auto* ir = programInfo->getCarriedIR();
  if (ir != nullptr) {
    // The recorded textures are the draw's whole sampler set: a view that is not instantiated yet
    // contributes no texture (the same rule the bind applies).
    for (const auto& proxy : ir->textures) {
      auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
      if (view == nullptr || view->isYUV()) {
        continue;
      }
      AddReadSwizzle(view->getTexture(), &required);
    }
  }
  return required;
}

// The color graph names the gradient node; scanning it avoids re-walking the processor tree the
// lowering already consumed.
bool GraphHasGradientSource(const AOTEffectGraph& graph) {
  for (size_t index = 0; index < graph.nodeCount(); ++index) {
    const auto* node = graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node != nullptr && node->kind == AOTEffectKind::GradientSource) {
      return true;
    }
  }
  return false;
}

}  // namespace

DrawShape ExtractDrawShape(const ProgramInfo* programInfo) {
  DrawShape facts = {};
  facts.alphaOnlyTarget = programInfo->getOutputSwizzle() == Swizzle::AAAA();
  const auto* ir = programInfo->getCarriedIR();
  // The IR's counts are the expression's: the processor lists no longer exist as a second
  // description, so the shape's processor counts come from the lowering that produced them.
  facts.colorFPCount = ir != nullptr ? ir->colorFPCount : 0;
  facts.fragmentCount = ir != nullptr ? ir->colorFPCount + ir->coverageFPCount : 0;
  facts.requiredTextureReads = RequiredTextureReads(programInfo);
  facts.coverage = ClassifyCoverageTerms(programInfo);
  facts.ir = ir;
  // The geometry parameters are read in place: the matching runs at prepare, where the layout the
  // rest of the upload addresses is already the truth.
  facts.geometry = &programInfo->getGeometryLayout().parameters;
  facts.xfer = &programInfo->getXferFacts();
  facts.hasGradientSource = ir != nullptr && GraphHasGradientSource(ir->color);
  return facts;
}

int DrawShape::xpType() const {
  if (xfer == nullptr || !xfer->shaderBlend()) {
    return XP_NONE;
  }
  const auto& proxy = xfer->dstTexture->textureProxy;
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  return view != nullptr ? XP_DST_TEXTURE : XP_FRAMEBUFFER_FETCH;
}

}  // namespace tgfx
