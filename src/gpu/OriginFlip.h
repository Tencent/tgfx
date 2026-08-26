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

#include "gpu/resources/RenderTarget.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

// Converts a rect from canvas top-left device space to the render target's backend scissor space.
// For ImageOrigin::TopLeft targets this is a no-op; for ImageOrigin::BottomLeft targets the rect
// is Y-flipped around the render target's height. Draw ops keep their scissorRect / stencil
// footprints in canvas top-left space throughout their lifetime and call this helper only at the
// backend boundary (immediately before RenderPass::setScissorRect), so that intermediate math
// (e.g. StencilCoverPathDrawOp::getStencilResolveBounds intersecting scissorRect with the cover
// device bounds) stays in a single, consistent coordinate space.
//
// Mirrors the transform produced by RenderTargetProxy::getOriginTransform(): a scale(1,-1)
// composed with translate(0, height). On a realized RenderTarget, height() equals the proxy's
// backingStoreHeight (see TextureRenderTargetProxy::onMakeTexture), so the two versions produce
// identical results.
inline void FlipYIfNeeded(Rect* rect, const RenderTarget* renderTarget) {
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    auto transform = Matrix::MakeScale(1.0f, -1.0f);
    transform.postTranslate(0.0f, static_cast<float>(renderTarget->height()));
    transform.mapRect(rect);
  }
}

}  // namespace tgfx
