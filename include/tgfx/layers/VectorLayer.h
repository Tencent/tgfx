/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * VectorLayer renders a tree of VectorElements, designed for creating animated vector graphics.
 * It provides a unified way to describe shapes with fill/stroke styles and various transformations.
 * The element tree is processed in order: geometry elements (shapes) provide paths,
 * styles (FillStyle/StrokeStyle) render the accumulated content, and modifiers
 * (TrimPath, RoundCorner, MergePath, Repeater) transform the paths before rendering. Each element
 * exposes animatable properties, making VectorLayer ideal for building complex motion graphics.
 */
class VectorLayer : public Layer {
 public:
  /**
   * Creates a new VectorLayer instance.
   */
  static std::shared_ptr<VectorLayer> Make();

  LayerType type() const override {
    return LayerType::Vector;
  }

  /**
   * Returns the root vector elements of this layer, similar to the contents of an AE shape layer.
   */
  const std::vector<std::shared_ptr<VectorElement>>& contents() const {
    return _contents;
  }

  /**
   * Sets the root vector elements of this layer. Each element can be a shape, a style, a modifier,
   * or a VectorGroup containing multiple elements.
   */
  void setContents(std::vector<std::shared_ptr<VectorElement>> value);

 protected:
  VectorLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  std::vector<std::shared_ptr<VectorElement>> _contents = {};
};

}  // namespace tgfx
