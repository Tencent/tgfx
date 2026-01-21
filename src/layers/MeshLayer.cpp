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

#include "tgfx/layers/MeshLayer.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

std::shared_ptr<MeshLayer> MeshLayer::Make() {
  return std::shared_ptr<MeshLayer>(new MeshLayer());
}

void MeshLayer::setMesh(std::shared_ptr<Mesh> mesh) {
  if (_mesh == mesh) {
    return;
  }
  _mesh = std::move(mesh);
  invalidateContent();
}

void MeshLayer::setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills) {
  if (_fillStyles.size() == fills.size() &&
      std::equal(_fillStyles.begin(), _fillStyles.end(), fills.begin())) {
    return;
  }
  _fillStyles = std::move(fills);
  invalidateContent();
}

void MeshLayer::removeFillStyles() {
  if (_fillStyles.empty()) {
    return;
  }
  _fillStyles = {};
  invalidateContent();
}

void MeshLayer::setFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    removeFillStyles();
  } else {
    setFillStyles({std::move(fillStyle)});
  }
}

void MeshLayer::addFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    return;
  }
  _fillStyles.push_back(std::move(fillStyle));
  invalidateContent();
}

void MeshLayer::onUpdateContent(LayerRecorder* recorder) {
  if (_mesh == nullptr) {
    return;
  }

  if (_fillStyles.empty()) {
    // Render with vertex colors only (use white as base color)
    recorder->addMesh(_mesh, LayerPaint(Color::White()));
  } else {
    // Note: If the mesh has vertex colors, they take priority over fillStyle colors.
    // The fillStyle shader (if any) will be modulated with vertex colors.
    for (const auto& style : _fillStyles) {
      LayerPaint paint(style->color(), style->blendMode());
      paint.shader = style->shader();
      recorder->addMesh(_mesh, paint);
    }
  }
}

}  // namespace tgfx
