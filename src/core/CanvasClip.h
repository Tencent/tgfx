/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/CanvasClipOp.h"
#include "tgfx/core/Path.h"

namespace tgfx {

/**
 * CanvasClipElement represents a single clipping element in the clip stack, which consists of a path, an operation
 * type, and whether to force anti-aliasing.
 */
class CanvasClipElement {
 public:
  CanvasClipElement(const Path& clip, CanvasClipOp op, bool forceAntiAlias);

 private:
  /**
   * The path of the clipping element
   */
  Path clip = {};

  /**
   * The operation type between the clipping element and the current entire clipping area, default is intersection
   * operation
   */
  CanvasClipOp op = CanvasClipOp::Intersect;

  /**
   * Whether anti-aliasing should be forcibly enabled on the edges of the clipping region, enabled by default. If the
   * Surface supports multisampling, anti-aliasing will be automatically enabled on the edges of the clipping region.
   * Set this value to true if you need to forcibly enable anti-aliasing when multisampling is not enabled.
  */
  bool forceAntiAlias = true;
};

class CanvasClip {
 public:
};

}  // namespace tgfx
