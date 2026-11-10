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

#include "emit/Assembler.h"

#include <cstring>

#include "gpu/shaders/Layout.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/SpecContract.h"

namespace tgfx {

namespace {

std::string DimMacro(const Module* module, const ModuleDim& dim) {
  if (ModuleIsImplicit(module)) {
    return dim.name;
  }
  return std::string(module->name) + "_" + dim.name;
}

void AppendBlockMember(const Module* module, const Element& element, uint16_t count,
                       std::string* text) {
  *text += "  ";
  *text += ShaderTypeGlslName(element.type);
  *text += " ";
  *text += ElementNameOf(module, element);
  if (count > 1) {
    *text += "[";
    *text += std::to_string(count);
    *text += "]";
  }
  *text += ";\n";
}

/// The variant's specialization prologue: the master switch, one constant per declared id, the
/// per-field accessors (constant when the runtime injected the values, the uniform otherwise) and
/// one unrolled executor macro per module declaring a site. The generated forms mirror the former
/// SpecCodegen output field for field, so the injectors and every backend keep their contracts.
void AppendSpecBlock(const Family& family, const Variant& variant, const VariantModules& modules,
                     std::string* text) {
  const auto specFields = DeriveSpecFields(family, variant);
  if (specFields.empty()) {
    return;
  }
  *text += "// The specialization contract: id 0 is the master switch; the runtime injects 1 plus\n";
  *text += "// the field values per draw shape, and the generic compile keeps the defaults.\n";
  *text += std::string("layout(constant_id = 0) const int ") + SpecContract::MasterSwitch +
           " = 0;\n";
  uint16_t nextId = 1;
  for (const auto& field : specFields) {
    const Module* module = modules.list[field.slot];
    const Element& element = module->elements[field.element];
    const uint16_t count = ElementCountOf(element, DimOffsetOf(family, variant, field.slot), variant);
    const uint32_t idCount = element.type == ShaderType::Int4 ? count * 4u : 1u;
    for (uint32_t offset = 0; offset < idCount; ++offset) {
      const std::string id = std::to_string(nextId + offset);
      *text += "layout(constant_id = " + id + ") const int " + SpecContract::ConstantPrefix +
               id + " = 0;\n";
    }
    nextId = static_cast<uint16_t>(nextId + idCount);
  }
  for (const auto& field : specFields) {
    const Module* module = modules.list[field.slot];
    const Element& element = module->elements[field.element];
    const std::string accessor =
        std::string(module->name) + "_" + SpecContract::FieldAccessorPrefix + element.name;
    const std::string fieldName = ElementNameOf(module, element);
    if (element.type == ShaderType::Int) {
      *text += "int " + accessor + "() { int s = " + SpecContract::MasterSwitch +
               "; if (s == 1) { return " + SpecContract::ConstantPrefix +
               std::to_string(field.firstId) + "; } return " + fieldName + "; }\n";
      continue;
    }
    const uint16_t count = ElementCountOf(element, DimOffsetOf(family, variant, field.slot), variant);
    *text += "ivec4 " + accessor + "(int i) {\n";
    *text += std::string("  int s = ") + SpecContract::MasterSwitch + ";\n";
    *text += "  if (s == 1) {\n";
    for (uint16_t member = 0; member < count; ++member) {
      const uint16_t base = static_cast<uint16_t>(field.firstId + member * 4);
      const std::string prefix = std::string("    if (i == ") + std::to_string(member) +
                                 ") { int a = " + SpecContract::ConstantPrefix;
      *text += prefix + std::to_string(base) + "; int b = " + SpecContract::ConstantPrefix +
               std::to_string(base + 1) + "; int c = " + SpecContract::ConstantPrefix +
               std::to_string(base + 2) + "; int d = " + SpecContract::ConstantPrefix +
               std::to_string(base + 3) + "; return ivec4(a, b, c, d); }\n";
    }
    *text += "    return ivec4(" + std::to_string(Op::None) + ", -2, -2, 0);\n";
    *text += "  }\n";
    *text += "  return " + fieldName + "[i];\n";
    *text += "}\n";
  }
}

/// The unrolled executor's macro: one guarded call site per slot of the variant's declared
/// capacity. A macro, not a function, so the sites stay statements in the calling hook's scope —
/// the same expansion the former tool pragma produced, now derived from the declaration.
void AppendUnrollMacro(const Family& family, const Variant& variant, const Module* module,
                       uint8_t slot, std::string* text) {
  const UnrollSite& site = *module->unroll;
  const Element& countElement = module->elements[site.countElement];
  const uint16_t capacity = ElementCountOf(module->elements[site.capacityElement],
                                           DimOffsetOf(family, variant, slot), variant);
  const std::string countAccessor =
      std::string(module->name) + "_" + SpecContract::FieldAccessorPrefix + countElement.name;
  *text += "#define " + std::string(module->name) + "_RUN_UNROLLED_SLOTS() \\\n";
  for (uint16_t slotIndex = 0; slotIndex < capacity; ++slotIndex) {
    *text += "  if (" + countAccessor + "() > " + std::to_string(slotIndex) + ") { " +
             site.function + "(" + std::to_string(slotIndex);
    if (site.arguments != nullptr && site.arguments[0] != '\0') {
      *text += ", " + std::string(site.arguments);
    }
    *text += "); }";
    *text += slotIndex + 1 < capacity ? " \\\n" : "\n";
  }
}

}  // namespace

AssembledVariant AssembleVariant(const Family& family, const Variant& variant) {
  const auto modules = ModulesOf(family, variant);
  AssembledVariant assembled;

  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr) {
      continue;
    }
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t dim = 0; dim < module->dimCount; ++dim) {
      const std::string define = DimMacro(module, module->dims[dim]) + "=" +
                                 std::to_string(variant.values[dimBase + dim]);
      assembled.vertDefines.push_back(define);
      assembled.fragDefines.push_back(define);
    }
  }

  std::string vert;
  vert += "// Assembled from the " + std::string(family.name) + " family declaration.\n";
  vert += "#version 450\n";
  vert += "struct Vertex { vec2 position; vec2 localCoord; };\n";
  vert += "layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform "
          "VertexUniformBlock {\n";
  std::string attributes;
  std::string varyings;
  std::string includes;
  std::string calls;
  uint8_t attributeLocation = 0;
  uint8_t varyingLocation = 0;
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t index = 0; index < module->elementCount; ++index) {
      const Element& element = module->elements[index];
      const bool present = ElementPresentInVariant(element, dimBase, variant);
      // Slotted kinds (attributes, instance attributes, varyings) number by their position among
      // all elements of the kind, so an absent one leaves a hole: the shader input then lands on
      // the geometry's own slot, mirroring GeometryProcessor::setVertexAttributes (a dropped
      // attribute keeps the declared slot numbering). Block members are not slotted.
      if (element.kind == Element::Kind::Attribute ||
          element.kind == Element::Kind::InstanceAttribute) {
        if (present) {
          attributes += "layout(location = " + std::to_string(attributeLocation) + ") in " +
                        ShaderTypeGlslName(element.type) + " " + ElementNameOf(module, element) +
                        ";\n";
        }
        attributeLocation++;
      } else if (element.kind == Element::Kind::Varying) {
        if (present) {
          varyings += "layout(location = " + std::to_string(varyingLocation) + ") out " +
                      ShaderTypeGlslName(element.type) + " " + VaryingNameOf(module, element) +
                      ";\n";
        }
        varyingLocation++;
      } else if (present && element.kind == Element::Kind::VertexParam) {
        AppendBlockMember(module, element, ElementCountOf(element, dimBase, variant),
                        &vert);
      }
    }
    if (module->hasVertex) {
      includes += "#include \"modules/" + std::string(module->name) + ".vert.glsl\"\n";
      calls += std::string("  ") + module->name + "_vertex(v);\n";
    }
  }
  vert += "};\n";
  vert += attributes;
  vert += varyings;
  vert += includes;
  vert += "void main() {\n";
  vert += "  Vertex v;\n";
  vert += calls;
  vert += "  gl_Position = vec4(v.position * RTAdjust.xz + RTAdjust.yw, 0.0, 1.0);\n";
  vert += "}\n";
  assembled.vertText = std::move(vert);

  std::string frag;
  frag += "// Assembled from the " + std::string(family.name) + " family declaration.\n";
  frag += "#version 450\n";
  frag += "struct Fragment { vec4 color; vec4 coverage; };\n";
  frag += "layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) "
          "uniform FragmentUniformBlock {\n";
  std::string fragVaryings;
  std::string samplers;
  includes.clear();
  calls.clear();
  uint8_t fragVaryingLocation = 0;
  uint8_t textureBinding = 0;
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    const uint8_t dimBase = DimOffsetOf(family, variant, slot);
    for (uint8_t index = 0; index < module->elementCount; ++index) {
      const Element& element = module->elements[index];
      const bool present = ElementPresentInVariant(element, dimBase, variant);
      // Varyings follow the same hole rule as the vertex stage (the full-order ordinal on both
      // stages); textures stay compact because the runtime binds them sequentially by order, not
      // by the declared number.
      if (element.kind == Element::Kind::Varying) {
        if (present) {
          fragVaryings += "layout(location = " + std::to_string(fragVaryingLocation) + ") in " +
                          ShaderTypeGlslName(element.type) + " " + VaryingNameOf(module, element) +
                          ";\n";
        }
        fragVaryingLocation++;
      } else if (present && element.kind == Element::Kind::Param) {
        AppendBlockMember(module, element, ElementCountOf(element, dimBase, variant),
                        &frag);
      } else if (present && element.kind == Element::Kind::Texture) {
        samplers += "layout(set = TGFX_TEXTURE_SET, binding = " + std::to_string(textureBinding) +
                    ") uniform sampler2D " + ElementNameOf(module, element) + ";\n";
        textureBinding++;
      }
    }
    if (module->hasFragment) {
      includes += "#include \"modules/" + std::string(module->name) + ".frag.glsl\"\n";
      calls += std::string("  ") + module->name + "_fragment(f);\n";
    }
  }
  frag += "};\n";
  frag += fragVaryings;
  frag += samplers;
  frag += "layout(location = 0) out vec4 fragColor;\n";
  AppendSpecBlock(family, variant, modules, &frag);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot) || module->unroll == nullptr) {
      continue;
    }
    AppendUnrollMacro(family, variant, module, slot, &frag);
  }
  frag += includes;
  frag += "void main() {\n";
  frag += "  Fragment f;\n";
  frag += calls;
  frag += "}\n";
  assembled.fragText = std::move(frag);

  return assembled;
}

}  // namespace tgfx
