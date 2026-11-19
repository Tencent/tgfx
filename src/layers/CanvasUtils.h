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

#include <memory>
#include <optional>
#include <vector>
#include "tgfx/core/Rect.h"

namespace tgfx {

class Canvas;
class DrawArgs;
class Layer;
class Shader;

/**
 * Returns the canvas clip bounds in the canvas's current local space, or std::nullopt if the canvas
 * cannot provide a meaningful clip (e.g. no canvas or no surface to derive bounds from).
 */
std::optional<Rect> GetClipBounds(const Canvas* canvas);

class LayerClip {
 public:
  /**
   * Applies the scroll rects and masks of the given ancestors to the canvas, from the outermost
   * ancestor to the innermost one. The canvas matrix must map the target's local space to device
   * space; it is restored on success, while the added clips stay on the canvas, so callers should
   * save and restore the canvas around this call. Masks that resolve to a clip path are applied as
   * canvas clips through Layer::prepareMask. When coverage is not nullptr, masks that resolve to a
   * shader are combined into a single device-space coverage shader written to it; when coverage is
   * nullptr, those shader masks are ignored. Returns false if the content is completely clipped
   * out, in which case the canvas matrix may be left in an ancestor's space.
   */
  static bool ApplyAncestorClips(const std::vector<Layer*>& clippedAncestors, Layer* target,
                                 const DrawArgs& args, Canvas* canvas,
                                 std::shared_ptr<Shader>* coverage);
};

}  // namespace tgfx
