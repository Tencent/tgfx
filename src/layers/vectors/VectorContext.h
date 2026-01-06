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
#include "Painter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

/**
 * VectorContext holds the rendering state while traversing vector elements.
 * This is an internal structure used by VectorLayer and VectorElement subclasses.
 */
struct VectorContext {
  /**
   * Adds a shape with an identity matrix to the list.
   */
  void addShape(std::shared_ptr<Shape> shape);

  /**
   * Shape list that can be modified by path modifiers.
   */
  std::vector<std::shared_ptr<Shape>> shapes = {};

  /**
   * Matrix list corresponding to each shape.
   */
  std::vector<Matrix> matrices = {};

  /**
   * Accumulated painters from style elements.
   */
  std::vector<std::unique_ptr<Painter>> painters = {};
};

}  // namespace tgfx
