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
    uint8_t arraySize = 1;
    UniformFormat format = UniformFormat::Float;
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

  /// The fields of the module at slotIndex (its slice of fields/moduleBase).
  struct Slice {
    const Field* fields = nullptr;
    const std::string* names = nullptr;
    uint16_t count = 0;
  };
  Slice SliceOf(uint8_t slotIndex) const;

  /// The index of the module's named element inside the slice, spelled <Module>_<name> (bare for
  /// the implicit Frame/Xfer). A wrong name returns the slice's count, which every writer treats
  /// as "field absent" and skips loudly in debug.
  uint16_t FieldIndexInSlice(const Slice& slice, const char* moduleName,
                             const char* elementName) const;
};

/// Derives the layout of one variant. The variant's family comes from Families() by index.
Layout DeriveLayout(const Variant& variant);

}  // namespace tgfx
