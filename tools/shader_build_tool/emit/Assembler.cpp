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

namespace tgfx {

namespace {

std::string DimMacro(const Module* module, const ModuleDim& dim) {
  if (ModuleIsImplicit(module)) {
    return dim.name;
  }
  return std::string(module->name) + "_" + dim.name;
}

void AppendBlockMember(const Module* module, const Element& element, std::string* text) {
  *text += "  ";
  *text += ShaderTypeGlslName(element.type);
  *text += " ";
  *text += ElementNameOf(module, element);
  if (element.count > 1) {
    *text += "[";
    *text += std::to_string(element.count);
    *text += "]";
  }
  *text += ";\n";
}

bool ElementInVariant(const Element& element, uint8_t dimBase, const Variant& variant) {
  if (element.when < 0) {
    return true;
  }
  return variant.values[dimBase + static_cast<uint8_t>(element.when)] != 0;
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
      if (!ElementInVariant(element, dimBase, variant)) {
        continue;
      }
      if (element.kind == Element::Kind::VertexParam) {
        AppendBlockMember(module, element, &vert);
      } else if (element.kind == Element::Kind::Attribute ||
                 element.kind == Element::Kind::InstanceAttribute) {
        attributes += "layout(location = " + std::to_string(attributeLocation) + ") in " +
                      ShaderTypeGlslName(element.type) + " " + ElementNameOf(module, element) +
                      ";\n";
        attributeLocation++;
      } else if (element.kind == Element::Kind::Varying) {
        varyings += "layout(location = " + std::to_string(varyingLocation) + ") out " +
                    ShaderTypeGlslName(element.type) + " " + VaryingNameOf(module, element) + ";\n";
        varyingLocation++;
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
      if (!ElementInVariant(element, dimBase, variant)) {
        continue;
      }
      if (element.kind == Element::Kind::Param) {
        AppendBlockMember(module, element, &frag);
      } else if (element.kind == Element::Kind::Varying) {
        fragVaryings += "layout(location = " + std::to_string(fragVaryingLocation) + ") in " +
                        ShaderTypeGlslName(element.type) + " " + VaryingNameOf(module, element) +
                        ";\n";
        fragVaryingLocation++;
      } else if (element.kind == Element::Kind::Texture) {
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
  frag += includes;
  frag += "void main() {\n";
  frag += "  Fragment f;\n";
  frag += calls;
  frag += "}\n";
  assembled.fragText = std::move(frag);

  return assembled;
}

}  // namespace tgfx
