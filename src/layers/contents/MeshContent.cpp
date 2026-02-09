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

#include "MeshContent.h"
#include "core/MeshImpl.h"
#include "core/ShapeMeshImpl.h"

namespace tgfx {

MeshContent::MeshContent(std::shared_ptr<Mesh> mesh, const LayerPaint& paint)
    : DrawContent(paint), mesh(std::move(mesh)) {
}

Rect MeshContent::onGetBounds() const {
  return mesh->bounds();
}

Rect MeshContent::getTightBounds(const Matrix& matrix) const {
  auto& impl = MeshImpl::ReadAccess(*mesh);
  if (impl.type() == MeshImpl::Type::Shape) {
    auto& shapeImpl = static_cast<const ShapeMeshImpl&>(impl);
    auto shape = shapeImpl.shape();
    if (shape != nullptr) {
      auto path = shape->getPath();
      path.transform(matrix);
      return path.getBounds();
    }
  }
  // Fallback for VertexMesh or when shape has been released
  auto bounds = mesh->bounds();
  return matrix.mapRect(bounds);
}

bool MeshContent::hitTestPoint(float localX, float localY) const {
  auto& impl = MeshImpl::ReadAccess(*mesh);
  if (impl.type() == MeshImpl::Type::Shape) {
    auto& shapeImpl = static_cast<const ShapeMeshImpl&>(impl);
    auto shape = shapeImpl.shape();
    if (shape != nullptr) {
      return shape->getPath().contains(localX, localY);
    }
  }
  // Fallback for VertexMesh or when shape has been released
  return mesh->bounds().contains(localX, localY);
}

void MeshContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawMesh(mesh, paint);
}

bool MeshContent::onHasSameGeometry(const GeometryContent* other) const {
  return mesh == static_cast<const MeshContent*>(other)->mesh;
}

}  // namespace tgfx
