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

#include <vector>
#include "Geometry.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

class LayerRecorder;

/**
 * Painter is the base class for objects that perform draw operations.
 */
class Painter {
 public:
  virtual ~Painter() = default;

  /**
   * Applies an alpha multiplier to this painter.
   */
  void applyAlpha(float groupAlpha) {
    alpha *= groupAlpha;
  }

  /**
   * Draws the geometries to the given recorder.
   */
  virtual void draw(LayerRecorder* recorder) = 0;

  /**
   * Creates a copy of this painter.
   */
  virtual std::unique_ptr<Painter> clone() const = 0;

  std::shared_ptr<Shader> shader = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  float alpha = 1.0f;
  LayerPlacement placement = LayerPlacement::Background;
  std::vector<Geometry*> geometries = {};
};

}  // namespace tgfx
