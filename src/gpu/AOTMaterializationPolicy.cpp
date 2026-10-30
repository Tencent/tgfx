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

#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/BackingFit.h"

namespace tgfx {

MaterializedGeometry AOTMaterializationPolicy::PrepareGeometry(const Rect& drawBounds,
                                                               float apronRadius) {
  auto bounds = drawBounds;
  bounds.outset(apronRadius, apronRadius);
  bounds.roundOut();
  MaterializedGeometry geometry = {};
  geometry.bounds = bounds;
  geometry.coordOffset = Point::Make(bounds.left, bounds.top);
  geometry.width = static_cast<int>(bounds.width());
  geometry.height = static_cast<int>(bounds.height());
  return geometry;
}

MaterializedTarget AOTMaterializationPolicy::AllocateTarget(Context* context,
                                                            const MaterializedGeometry& geometry) {
  if (context == nullptr || geometry.isEmpty()) {
    return {};
  }
  // BackingFit::Exact is required, not a preference: consumers sample this texture with a
  // translate-only matrix, which is only correct when the backing texture is exactly width x height.
  // An Approx (rounded-up) backing would make the normalized coordinates address a scaled region and
  // read uninitialized padding.
  auto renderTarget = RenderTargetProxy::Make(context, geometry.width, geometry.height, false, 1,
                                              false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return {};
  }
  MaterializedTarget target = {};
  target.renderTarget = std::move(renderTarget);
  target.geometry = geometry;
  return target;
}

}  // namespace tgfx
