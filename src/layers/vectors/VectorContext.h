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
 * This is an internal structure used by VectorLayer and VectorElement subclasses.
 */
struct VectorContext {
  /**
   * Adds a shape geometry with an identity matrix to the list.
   */
  void addShape(std::shared_ptr<Shape> shape);

  /**
   * Adds a text geometry with an identity matrix to the list.
   */
  void addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position);

  /**
   * Converts all geometries to ShapeGeometry and returns them. Non-convertible geometries are
   * removed. After this call, the geometries list contains only ShapeGeometry instances.
   */
  std::vector<ShapeGeometry*> getShapeGeometries();

  /**
   * Geometry list that can be modified by modifiers.
   */
  std::vector<std::unique_ptr<Geometry>> geometries = {};

  /**
   * Matrix list corresponding to each geometry. Path modifiers only modify geometries,
   * not matrices. This allows stroke to be applied before outer group transforms.
   */
  std::vector<Matrix> matrices = {};

  /**
   * Accumulated painters from style elements.
   */
  std::vector<std::unique_ptr<Painter>> painters = {};
};

}  // namespace tgfx
