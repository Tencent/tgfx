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

namespace tgfx {

namespace {

size_t AlignCursor(size_t cursor, size_t alignment) {
  return (cursor + alignment - 1) / alignment * alignment;
}

/// The total std140 size of an element: arrays stride each element up to a 16-byte multiple and
/// align the array itself to 16, matching UniformData's packing of the reflected fields.
size_t ElementTotalSize(const Element& element) {
  const size_t elementSize = ShaderTypeSize(element.type);
  if (element.count <= 1) {
    return elementSize;
  }
  const size_t stride = (elementSize + 15) / 16 * 16;
  return stride * element.count;
}

bool ElementPresent(const Element& element, uint8_t dimBase, const Variant& variant) {
  if (element.when < 0) {
    return true;
  }
  return variant.values[dimBase + static_cast<uint8_t>(element.when)] != 0;
}

}  // namespace

Layout DeriveLayout(const Variant& variant) {
  const auto* familyTable = Families();
  const Family& family = familyTable[variant.family];
  const auto modules = ModulesOf(family, variant);

  Layout layout;
  layout.moduleBase.resize(modules.count, 0);
  layout.moduleFieldCount.resize(modules.count, 0);
  size_t vertexCursor = 0;
  size_t fragmentCursor = 0;
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    layout.moduleBase[slot] = static_cast<uint16_t>(layout.fields.size());
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    const uint16_t firstField = static_cast<uint16_t>(layout.fields.size());
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t index = 0; index < module->elementCount; ++index) {
      const Element& element = module->elements[index];
      if (!ElementPresent(element, dimBase, variant)) {
        continue;
      }
      const std::string name = ElementNameOf(module, element);
      switch (element.kind) {
        case Element::Kind::Attribute:
        case Element::Kind::InstanceAttribute: {
          Layout::Attribute attribute = {};
          attribute.name = name;
          attribute.format = ShaderTypeFormat(element.type);
          attribute.location = static_cast<uint8_t>(layout.attributes.size());
          layout.attributes.push_back(attribute);
          break;
        }
        case Element::Kind::Varying: {
          Layout::Varying varying = {};
          varying.name = VaryingNameOf(module, element);
          varying.location = static_cast<uint8_t>(layout.varyings.size());
          layout.varyings.push_back(varying);
          break;
        }
        case Element::Kind::Texture: {
          Layout::Texture texture = {};
          texture.name = name;
          texture.binding = static_cast<uint8_t>(layout.textures.size());
          layout.textures.push_back(texture);
          break;
        }
        case Element::Kind::VertexParam:
        case Element::Kind::Param: {
          Layout::Field field = {};
          field.block = element.kind == Element::Kind::VertexParam ? 0 : 1;
          field.format = ShaderTypeFormat(element.type);
          field.arraySize = element.count;
          field.size = static_cast<uint8_t>(ElementTotalSize(element));
          size_t alignment = ShaderTypeAlign(element.type);
          if (element.count > 1 && alignment < 16) {
            alignment = 16;
          }
          size_t& cursor = field.block == 0 ? vertexCursor : fragmentCursor;
          cursor = AlignCursor(cursor, alignment);
          field.offset = static_cast<uint16_t>(cursor);
          cursor += field.size;
          layout.fields.push_back(field);
          layout.fieldNames.push_back(name);
          break;
        }
      }
    }
    layout.moduleFieldCount[slot] = static_cast<uint16_t>(layout.fields.size() - firstField);
  }
  layout.vertexBlockSize = static_cast<uint16_t>(AlignCursor(vertexCursor, 16));
  layout.fragmentBlockSize = static_cast<uint16_t>(AlignCursor(fragmentCursor, 16));
  return layout;
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

uint16_t Layout::FieldIndexInSlice(const Slice& slice, const char* moduleName,
                                   const char* elementName) const {
  const std::string fullName = std::string(moduleName) + "_" + elementName;
  for (uint16_t index = 0; index < slice.count; ++index) {
    if (slice.names[index] == fullName || slice.names[index] == elementName) {
      return index;
    }
  }
  return slice.count;
}

}  // namespace tgfx
