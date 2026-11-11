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

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/shaders/Family.h"

namespace tgfx {

/// The interface of one variant, derived as a pure function of the declaration: the single
/// numbering source for UBO offsets, attribute and varying locations, texture bindings. The
/// tool's assembled GLSL and the runtime's parameter writes both read these numbers, and the
/// build fails when the reflected interface disagrees (the only interface check).
struct Layout {
  struct Field {
    /// Byte offset inside the field's uniform block.
    uint16_t offset = 0;
    /// 0 = VertexUniformBlock, 1 = FragmentUniformBlock.
    uint8_t block = 0;
    /// The element's whole std140 footprint: an array of 17 vec4 is 272 bytes, so this is 16 bits
    /// (the first array element to exceed 255 bytes was the blur kernel table).
    uint16_t size = 0;
    /// The element count: 16 bits because the slot interpreter's data pool reaches exactly 256
    /// (32 slots * 7 words + the 32-word block region); the bundle codec already stores it wide.
    uint16_t arraySize = 1;
    UniformFormat format = UniformFormat::Float;
  };
  /// One specialization field of the variant, in constant-id order (id 0 is the master switch).
  /// `element` is the index in the slot's module declaration; the element's count says how many
  /// ids it owns (an Int4 array takes four per element).
  struct SpecField {
    uint16_t firstId = 1;
    uint8_t slot = 0;
    uint8_t element = 0;
  };
  struct Attribute {
    std::string name;
    UniformFormat format = UniformFormat::Float;
    uint8_t location = 0;
  };
  struct Varying {
    std::string name;
    uint8_t location = 0;
  };
  struct Texture {
    std::string name;
    uint8_t binding = 0;
  };

  /// One entry per (module, element) present in the variant, in module order.
  std::vector<Field> fields;
  std::vector<std::string> fieldNames;
  /// Per module slot, per element of the slot's module: the index of the element's uniform field
  /// in `fields`, or NoField when the element is absent in this variant (its `when` dim excludes
  /// it) or contributes no field (attributes, varyings, textures). One derivation per program
  /// replaces every per-draw name lookup.
  std::vector<std::vector<uint16_t>> elementField;
  /// The elementField entry for an element with no uniform field in this variant.
  static constexpr uint16_t NoField = 0xFFFF;
  /// The first field index of each module slot (slot 0 is Frame; the value is fields.size() when
  /// the slot contributes no fields).
  std::vector<uint16_t> moduleBase;
  /// The field count of each module slot (0 for absent modules).
  std::vector<uint16_t> moduleFieldCount;
  uint16_t vertexBlockSize = 0;
  uint16_t fragmentBlockSize = 0;
  std::vector<Attribute> attributes;
  std::vector<Varying> varyings;
  std::vector<Texture> textures;
  /// The variant's specialization fields, in id order (see DeriveSpecFields).
  std::vector<SpecField> specFields;

  /// The fields of the module at slotIndex (its slice of fields/moduleBase).
  struct Slice {
    const Field* fields = nullptr;
    const std::string* names = nullptr;
    uint16_t count = 0;
  };
  Slice SliceOf(uint8_t slotIndex) const;
};

/// Derives the layout of one variant of `family` (the explicit form the module tests use; the
/// tool and the writer always pass the table row the variant names).
Layout DeriveLayout(const Family& family, const Variant& variant);

/// Derives the layout of one variant. The variant's family comes from Families() by index.
Layout DeriveLayout(const Variant& variant);

/// The variant's spec elements in id order: id 0 is the master switch, then one entry per spec
/// element by module order, then element order (a scalar Int owns 1 id, an Int4 array four per
/// element). The one allocation rule: DeriveLayout fills Layout::specFields through it and the
/// assembler generates the constant declarations and accessors from it, so the two cannot drift.
std::vector<Layout::SpecField> DeriveSpecFields(const Family& family, const Variant& variant);

}  // namespace tgfx
