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
#include <vector>
#include "Geometry.h"
#include "Painter.h"

namespace tgfx {

/**
 * VectorContext holds the rendering state while traversing vector elements.
 * This is an internal class used by VectorLayer and VectorElement subclasses.
 */
class VectorContext {
 public:
  /**
   * Adds a shape geometry to the list.
   */
  void addShape(std::shared_ptr<Shape> shape);

  /**
   * Adds a text geometry with the given position and optional text anchors to the list.
   */
  void addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position,
                   const std::vector<Point>& textAnchors = {});

  /**
   * Converts all geometries to shape mode and returns the geometry list.
   * Text and glyph content is converted to Shape. Call this before applying path modifiers.
   */
  std::vector<Geometry*> getShapeGeometries();

  /**
   * Expands text geometries to glyph mode and returns geometries that have text content.
   * Pure shape geometries are not included. Call this before applying text modifiers.
   */
  std::vector<Geometry*> getGlyphGeometries();

  /**
   * Geometry list with ownership.
   */
  std::vector<std::unique_ptr<Geometry>> geometries = {};

  /**
   * Accumulated painters from style elements.
   */
  std::vector<std::unique_ptr<Painter>> painters = {};
};

}  // namespace tgfx
