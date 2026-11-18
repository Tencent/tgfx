/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "emit/ComposedShapes.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/modules/Chain.h"

namespace tgfx {
namespace {

ChainCompositionSlot Slot(int op, int in0, int in1, int outRegister, int selector) {
  ChainCompositionSlot slot = {};
  slot.op = op;
  slot.in0 = in0;
  slot.in1 = in1;
  slot.outRegister = outRegister;
  slot.selector = selector;
  return slot;
}

// A plain leaf sample modulated by the geometry color's alpha: bit 0 of the selector, result
// register 0.
constexpr int kPlainLeafSelector = 1 | (1 << 20);

}  // namespace

// PART_MANIFEST

const std::vector<ComposedShape>& ComposedShapeManifest() {
  static const std::vector<ComposedShape> manifest = [] {
    std::vector<ComposedShape> shapes = {};

    ComposedShape fill = {};
    fill.name = "fill";
    fill.profile = 0;
    fill.composition.slots = {Slot(ChainISA::ConstColor, -4, -2, 0, 0)};
    fill.composition.rootRegister = 0;
    fill.composition.usedOps = 1u << ChainISA::ConstColor;
    shapes.push_back(fill);

    ComposedShape texture = {};
    texture.name = "texture";
    texture.profile = 2;
    texture.composition.slots = {Slot(ChainISA::Texture, -2, -2, 0, kPlainLeafSelector)};
    texture.composition.rootRegister = 0;
    texture.composition.usedOps = 1u << ChainISA::Texture;
    shapes.push_back(texture);

    ComposedShape textureMatrix = {};
    textureMatrix.name = "texture-colormatrix";
    textureMatrix.profile = 2;
    textureMatrix.composition.slots = {Slot(ChainISA::Texture, -2, -2, 0, kPlainLeafSelector),
                                       Slot(ChainISA::ColorMatrix, 0, -2, 0, 0)};
    textureMatrix.composition.rootRegister = 0;
    textureMatrix.composition.usedOps = (1u << ChainISA::Texture) | (1u << ChainISA::ColorMatrix);
    shapes.push_back(textureMatrix);

    ComposedShape blend = {};
    blend.name = "blend";
    blend.profile = 0;
    auto blendSlot = Slot(ChainISA::Blend, 0, 1, 2, 0);
    blendSlot.blendMode = 3;
    blendSlot.multiplyInputAlpha = true;
    blend.composition.slots = {Slot(ChainISA::ConstColor, -4, -2, 0, 0),
                               Slot(ChainISA::ConstColor, -4, -2, 1, 0), blendSlot};
    blend.composition.rootRegister = 2;
    blend.composition.usedOps = (1u << ChainISA::ConstColor) | (1u << ChainISA::Blend);
    blend.composition.blendModes = 1u << 3;
    shapes.push_back(blend);

    ComposedShape coverage = {};
    coverage.name = "coverage";
    coverage.profile = 0;
    coverage.composition.slots = {Slot(ChainISA::ConstColor, -4, -2, 0, 0),
                                  Slot(ChainISA::AARectCoverage, -3, -2, 1, 0)};
    coverage.composition.rootRegister = 0;
    coverage.composition.coverageRootRegister = 1;
    coverage.composition.clipCoverageRegister = 1;
    coverage.composition.usedOps = (1u << ChainISA::ConstColor) | (1u << ChainISA::AARectCoverage);
    shapes.push_back(coverage);

    ComposedShape gradient = {};
    gradient.name = "gradient";
    gradient.profile = 3;
    gradient.composition.slots = {Slot(ChainISA::Gradient, -3, -2, 0, 0)};
    gradient.composition.rootRegister = 0;
    gradient.composition.usedOps = 1u << ChainISA::Gradient;
    shapes.push_back(gradient);

    return shapes;
  }();
  return manifest;
}

// PART_ID_VALUES

std::vector<std::pair<uint32_t, int32_t>> ComposedSpecIdValues(
    const Family& family, const Variant& variant, const ChainComposition& composition) {
  // The variant's PROFILE fixes the SlotPacked capacity, the same table the declaration sizes the
  // pool from; the inactive tail pads to it inside the value resolution.
  const size_t capacity = ChainSlotPackedCounts[DimValueOf(family, variant, 2, 0)];
  const auto values = ResolveCompositionSpecValues(composition, capacity);
  const auto fields = DeriveLayout(variant).specFields;
  std::vector<std::pair<uint32_t, int32_t>> idValues = {};
  for (const auto& field : fields) {
    const Module* module = ModuleAt(family, variant, field.slot);
    const auto& element = module->elements[field.element];
    const auto found = values.find(ElementNameOf(module, element));
    if (found == values.end()) {
      continue;
    }
    const uint16_t count = ElementCountOf(element, DimOffsetOf(family, variant, field.slot));
    const uint32_t words = element.type == ShaderType::Int4 ? count * 4u : 1u;
    for (uint32_t word = 0; word < words; ++word) {
      idValues.emplace_back(field.firstId + word,
                            word < found->second.size() ? found->second[word] : 0);
    }
  }
  return idValues;
}

}  // namespace tgfx
