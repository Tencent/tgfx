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

#include "DataSource.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

/**
 * ShapeVertexSource converts a Shape into triangulated vertex data for Mesh rendering.
 * Unlike ShapeRasterizer, it only performs triangulation without fallback to rasterization.
 */
class ShapeVertexSource : public DataSource<Data> {
 public:
  ShapeVertexSource(std::shared_ptr<Shape> shape, bool antiAlias);

  std::shared_ptr<Data> getData() const override;

 private:
  std::shared_ptr<Shape> shape = nullptr;
  bool antiAlias = true;
};

}  // namespace tgfx
