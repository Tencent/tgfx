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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/LeafWrite.h"
#include "gpu/CoordTransform.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/GeometryParameters.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

Matrix TotalLeafMatrix(const AOTTextureParameters& leaf) {
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  return TotalCoordMatrix(leaf.uvMatrix, view.get());
}

Matrix SourceCoordMatrix(const AOTTextureParameters& leaf, const GeometryLayout& layout) {
  // Only the kinds whose processors concatenated their uv matrix onto the source coordinate.
  Matrix uvMatrix = Matrix::I();
  const auto& geometry = layout.parameters;
  if (geometry.kind == GeometryKind::Default || geometry.kind == GeometryKind::QuadPerEdgeAA) {
    uvMatrix = geometry.uvMatrix;
  }
  Matrix combined = {};
  combined.setConcat(TotalLeafMatrix(leaf), uvMatrix);
  return combined;
}

}  // namespace tgfx
