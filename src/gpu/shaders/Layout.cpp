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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/Layout.h"
#include <cassert>
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {

// The layout's 16-bit Field::arraySize must cover the widest declared array: the slot
// interpreter's data pool (32 slots * 7 words + the 32-word block region) is exactly 256 today,
// one past a byte. The assert lives here rather than in ChainOps.h because the tool's source
// digest covers the chain contract headers — a digest change invalidates every bundle even when
// the compiled text is identical, and this is a width fact of the layout, not of the pool.
static_assert(32 * ChainISA::PoolSlotWords + ChainISA::BlockWords <= 0xFFFF,
              "the chain data pool exceeds the layout's 16-bit array size");

namespace {

size_t AlignCursor(size_t cursor, size_t alignment) {
  return (cursor + alignment - 1) / alignment * alignment;
}

/// The total std140 size of an element: arrays stride each element up to a 16-byte multiple and
/// align the array itself to 16, matching UniformData's packing of the reflected fields.
size_t ElementTotalSize(const Element& element, uint16_t count) {
  const size_t elementSize = ShaderTypeSize(element.type);
  if (count <= 1) {
    return elementSize;
  }
  const size_t stride = (elementSize + 15) / 16 * 16;
  return stride * count;
}

bool ElementPresent(const Element& element, uint8_t dimBase, const Variant& variant) {
  // A gated element with an empty mask can never exist: a declaration bug the variant walk
  // catches on its first evaluation (the bundle build derives every variant).
  assert(element.when < 0 || element.whenMask != 0);
  return ElementPresentInVariant(element, dimBase, variant);
}

}  // namespace

Layout DeriveLayout(const Variant& variant) {
  return DeriveLayout(Families()[variant.family], variant);
}

Layout DeriveLayout(const Family& family, const Variant& variant) {
  const auto modules = ModulesOf(family, variant);

  Layout layout;
  layout.moduleBase.resize(modules.count, 0);
  layout.moduleFieldCount.resize(modules.count, 0);
  layout.elementField.resize(modules.count);
  size_t vertexCursor = 0;
  size_t fragmentCursor = 0;
  // The same hole rule the assembler spells: attributes, instance attributes and varyings carry
  // their full-order ordinal whether present or not, so a present element lands on its slot and
  // the derived layout agrees with the assembled text. Textures and block members stay compact.
  uint8_t attributeOrdinal = 0;
  uint8_t varyingOrdinal = 0;
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    layout.moduleBase[slot] = static_cast<uint16_t>(layout.fields.size());
    layout.elementField[slot].assign(module != nullptr ? module->elementCount : 0, Layout::NoField);
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    const uint16_t firstField = static_cast<uint16_t>(layout.fields.size());
    uint16_t presentFieldCount = 0;
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t index = 0; index < module->elementCount; ++index) {
      const Element& element = module->elements[index];
      const bool present = ElementPresent(element, dimBase, variant);
      if (element.kind == Element::Kind::Attribute ||
          element.kind == Element::Kind::InstanceAttribute) {
        if (present) {
          Layout::Attribute attribute = {};
          attribute.name = ElementNameOf(module, element);
          attribute.format = ShaderTypeFormat(element.type);
          attribute.location = attributeOrdinal;
          layout.attributes.push_back(attribute);
        }
        attributeOrdinal++;
      } else if (element.kind == Element::Kind::Varying) {
        if (present) {
          Layout::Varying varying = {};
          varying.name = VaryingNameOf(module, element);
          varying.location = varyingOrdinal;
          layout.varyings.push_back(varying);
        }
        varyingOrdinal++;
      } else if (present) {
        const std::string name = ElementNameOf(module, element);
        switch (element.kind) {
          case Element::Kind::Texture: {
            Layout::Texture texture = {};
            texture.name = name;
            texture.binding = static_cast<uint8_t>(layout.textures.size());
            layout.textures.push_back(texture);
            break;
          }
          case Element::Kind::VertexParam:
          case Element::Kind::Param: {
            const uint16_t count = ElementCountOf(element, dimBase, variant);
            Layout::Field field = {};
            field.block = element.kind == Element::Kind::VertexParam ? 0 : 1;
            field.format = ShaderTypeFormat(element.type);
            field.arraySize = count;
            field.size = static_cast<uint16_t>(ElementTotalSize(element, count));
            field.elementSize = static_cast<uint16_t>(ShaderTypeSize(element.type));
            size_t alignment = ShaderTypeAlign(element.type);
            if (count > 1 && alignment < 16) {
              alignment = 16;
            }
            size_t& cursor = field.block == 0 ? vertexCursor : fragmentCursor;
            cursor = AlignCursor(cursor, alignment);
            field.offset = static_cast<uint16_t>(cursor);
            cursor += field.size;
            layout.fields.push_back(field);
            layout.fieldNames.push_back(name);
            // The element's field, for the element-indexed writes (SlotWriter).
            layout.elementField[slot][index] =
                static_cast<uint16_t>(firstField + presentFieldCount);
            ++presentFieldCount;
            break;
          }
          case Element::Kind::Attribute:
          case Element::Kind::InstanceAttribute:
          case Element::Kind::Varying:
            break;
        }
      }
    }
    layout.moduleFieldCount[slot] = static_cast<uint16_t>(layout.fields.size() - firstField);
  }
  layout.vertexBlockSize = static_cast<uint16_t>(AlignCursor(vertexCursor, 16));
  layout.fragmentBlockSize = static_cast<uint16_t>(AlignCursor(fragmentCursor, 16));
  layout.specFields = DeriveSpecFields(family, variant);
  return layout;
}

std::vector<Layout::SpecField> DeriveSpecFields(const Family& family, const Variant& variant) {
  std::vector<Layout::SpecField> specFields;
  const auto modules = ModulesOf(family, variant);
  // Id 0 is the master switch; the fields follow in module order, then element order.
  uint16_t nextId = 1;
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t index = 0; index < module->elementCount; ++index) {
      const Element& element = module->elements[index];
      if (!element.spec) {
        continue;
      }
      // A spec element is part of the program interface: it must be a present uniform field in
      // every variant of the family (no `when` gate), delivered as an Int scalar or an Int4 array.
      assert(element.when < 0);
      assert(element.kind == Element::Kind::Param);
      assert(element.type == ShaderType::Int || element.type == ShaderType::Int4);
      const uint16_t count = ElementCountOf(element, dimBase, variant);
      assert((element.type == ShaderType::Int && count == 1) ||
             (element.type == ShaderType::Int4 && count > 1));
      Layout::SpecField field = {};
      field.firstId = nextId;
      field.slot = slot;
      field.element = index;
      specFields.push_back(field);
      // A scalar owns one id; an ivec4 array element owns four (the constant-id slots the
      // backends specialize per component).
      nextId = static_cast<uint16_t>(nextId + (element.type == ShaderType::Int4 ? count * 4 : 1));
    }
  }
  return specFields;
}

Layout::Slice Layout::SliceOf(uint8_t slotIndex) const {
  Slice slice = {};
  if (slotIndex >= moduleBase.size() || slotIndex >= moduleFieldCount.size()) {
    return slice;
  }
  const uint16_t first = moduleBase[slotIndex];
  slice.fields = fields.data() + first;
  slice.names = fieldNames.data() + first;
  slice.count = moduleFieldCount[slotIndex];
  return slice;
}

}  // namespace tgfx
