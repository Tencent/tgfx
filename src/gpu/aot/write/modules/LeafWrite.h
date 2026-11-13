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

#pragma once

#include <array>
#include <cstdint>
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/shaders/Module.h"

namespace tgfx {

class SlotWriter;
struct AOTTextureParameters;
struct GeometryLayout;

/// The leaf's total transform: the raw matrix followed by the instantiated view's normalization
/// and origin flip (the tree-side CoordTransform::getTotalMatrix(), without the geometry uv).
Matrix TotalLeafMatrix(const AOTTextureParameters& leaf);

/// The source coordinate matrix every texture leaf shares: the leaf's total transform with the
/// geometry's uv matrix pre-concatenated (setTransformDataHelper's composition).
Matrix SourceCoordMatrix(const AOTTextureParameters& leaf, const GeometryLayout& layout);

/// The leaf's subset rect and read gates, the same values BindTextureSourceLeaf uploads. The
/// texture-carrying modules declare different subsets of these fields, so the shared body
/// templates on the module and writes exactly the fields it declares (a compile-time fact).
template <const Module& module>
void WriteTextureLeafParams(const AOTTextureParameters& leaf, SlotWriter* writer) {
  constexpr uint8_t Subset = UniformElementIndexOf(module, "Subset");
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  float rect[4] = {};
  LeafSubsetRectForSampler(leaf.textureProxy, view.get(), leaf.subset, leaf.samplerState, rect);
  writer->set(Subset, std::array<float, 4>{rect[0], rect[1], rect[2], rect[3]});
  if constexpr (HasElement(module, "SubsetClampOn")) {
    constexpr uint8_t SubsetClampOn = UniformElementIndexOf(module, "SubsetClampOn");
    writer->set(SubsetClampOn,
                static_cast<int32_t>(
                    LeafSubsetClampOn(leaf.textureProxy, view.get(), leaf.subset) ? 1 : 0));
  }
  if constexpr (HasElement(module, "AlphaStart")) {
    constexpr uint8_t AlphaStart = UniformElementIndexOf(module, "AlphaStart");
    if (leaf.hasRGBAAA) {
      auto coord = view->getTextureCoord(leaf.alphaStart.x, leaf.alphaStart.y);
      writer->set(AlphaStart, std::array<float, 2>{coord.x, coord.y});
    }
  }
  if constexpr (HasElement(module, "AlphaOnly")) {
    constexpr uint8_t AlphaOnly = UniformElementIndexOf(module, "AlphaOnly");
    writer->set(AlphaOnly, static_cast<int32_t>(leaf.isAlphaOnly ? 1 : 0));
  }
  if constexpr (HasElement(module, "HasRgbaaa")) {
    constexpr uint8_t HasRgbaaa = UniformElementIndexOf(module, "HasRgbaaa");
    writer->set(HasRgbaaa, static_cast<int32_t>(leaf.hasRGBAAA ? 1 : 0));
  }
}

}  // namespace tgfx
