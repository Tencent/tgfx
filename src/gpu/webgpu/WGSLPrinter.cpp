////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "WGSLPrinter.h"
#include <string>
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ir/ShaderLanguageNaming.h"

namespace tgfx {
namespace {

constexpr char Indent[] = "  ";

const char* WgslType(SLType type) {
  const char* name = ValueTypeName(ShaderLibraryLanguage::WGSL, type);
  return name != nullptr ? name : "vec4<f32>";
}

bool HasUniforms(const InterfaceDesc& interface, ShaderStage stage) {
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage == stage) {
      return true;
    }
  }
  return false;
}

bool IsWordChar(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '_';
}

void ReplaceWord(std::string* text, const char* from, const char* to) {
  const std::string needle(from);
  const std::string replacement(to);
  size_t offset = 0;
  while ((offset = text->find(needle, offset)) != std::string::npos) {
    const bool leftOk = offset == 0 || !IsWordChar((*text)[offset - 1]);
    const size_t end = offset + needle.size();
    const bool rightOk = end >= text->size() || !IsWordChar((*text)[end]);
    if (leftOk && rightOk) {
      text->replace(offset, needle.size(), replacement);
      offset += replacement.size();
    } else {
      offset = end;
    }
  }
}

void AppendUniformStruct(std::string* out, const char* blockName, ShaderStage stage,
                         const InterfaceDesc& interface) {
  if (!HasUniforms(interface, stage)) {
    return;
  }
  *out += "struct ";
  *out += blockName;
  *out += " {\n";
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage != stage) {
      continue;
    }
    *out += Indent;
    *out += uniform.name;
    *out += ": ";
    if (uniform.count > 1) {
      *out += std::string("array<") + WgslType(uniform.type) + ", " +
              std::to_string(uniform.count) + ">";
    } else {
      *out += WgslType(uniform.type);
    }
    *out += ",\n";
  }
  *out += "};\n\n";
}

void AppendSamplers(std::string* out, const InterfaceDesc& interface) {
  for (size_t index = 0; index < interface.samplers.size(); ++index) {
    const auto& sampler = interface.samplers[index];
    const std::string group = std::to_string(TEXTURE_DESCRIPTOR_SET);
    *out += "@group(" + group + ") @binding(" + std::to_string(index * 2) + ") var " +
            sampler.name + ": texture_2d<f32>;\n";
    *out += "@group(" + group + ") @binding(" + std::to_string(index * 2 + 1) + ") var " +
            sampler.name + "_Sampler: sampler;\n";
  }
  if (!interface.samplers.empty()) {
    *out += "\n";
  }
}

void AppendVaryingField(std::string* out, const VaryingDesc& varying, size_t index) {
  *out += Indent;
  *out += "@location(" + std::to_string(index) + ") ";
  if (varying.flat) {
    *out += "@interpolate(flat) ";
  }
  *out += varying.name;
  *out += ": ";
  *out += WgslType(varying.type);
  *out += ",\n";
}

}  // namespace

std::string WGSLPrinter::PrintVertex(const InterfaceDesc& interface, const std::string& functions,
                                     const std::string& body, const WgslCallSite& callSite) {
  (void)callSite;
  std::string out = {};
  if (!functions.empty()) {
    out += functions;
    out += "\n";
  }
  AppendUniformStruct(&out, VertexUniformBlockName, ShaderStage::Vertex, interface);
  if (HasUniforms(interface, ShaderStage::Vertex)) {
    out += "@group(" + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) + ") @binding(" +
           std::to_string(VERTEX_UBO_BINDING_POINT) + ") var<uniform> vb: ";
    out += VertexUniformBlockName;
    out += ";\n\n";
  }

  if (!interface.attributes.empty()) {
    out += "struct VertexIn {\n";
    for (size_t index = 0; index < interface.attributes.size(); ++index) {
      const auto& attribute = interface.attributes[index];
      out += Indent;
      out += "@location(" + std::to_string(index) + ") " + attribute.name + ": " +
             WgslType(attribute.type) + ",\n";
    }
    out += "};\n\n";
  }

  out += "struct VertexOut {\n";
  out += std::string(Indent) + "@builtin(position) position: vec4<f32>,\n";
  for (size_t index = 0; index < interface.varyings.size(); ++index) {
    AppendVaryingField(&out, interface.varyings[index], index);
  }
  out += "};\n\n";

  out += "@vertex\nfn main(";
  if (!interface.attributes.empty()) {
    out += "in: VertexIn";
  }
  out += ") -> VertexOut {\n";
  out += std::string(Indent) + "var out: VertexOut;\n";
  for (const auto& attribute : interface.attributes) {
    out += Indent + std::string("let ") + attribute.name + " = in." + attribute.name + ";\n";
  }
  if (HasUniforms(interface, ShaderStage::Vertex)) {
    for (const auto& uniform : interface.uniforms) {
      if (uniform.stage == ShaderStage::Vertex) {
        out += Indent + std::string("let ") + uniform.name + " = vb." + uniform.name + ";\n";
      }
    }
  }
  for (const auto& varying : interface.varyings) {
    out += Indent + std::string("var ") + varying.name + ": " + WgslType(varying.type) + ";\n";
  }
  out += body;
  for (const auto& varying : interface.varyings) {
    out += Indent + std::string("out.") + varying.name + " = " + varying.name + ";\n";
  }
  out += std::string(Indent) + "return out;\n}\n";
  return out;
}

std::string WGSLPrinter::PrintFragment(const InterfaceDesc& interface,
                                       const std::string& functions, const std::string& body,
                                       const WgslCallSite& callSite) {
  (void)callSite;
  std::string out = {};
  if (!functions.empty()) {
    out += functions;
    out += "\n";
  }
  AppendUniformStruct(&out, FragmentUniformBlockName, ShaderStage::Fragment, interface);
  if (HasUniforms(interface, ShaderStage::Fragment)) {
    out += "@group(" + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET) + ") @binding(" +
           std::to_string(FRAGMENT_UBO_BINDING_POINT) + ") var<uniform> fb: ";
    out += FragmentUniformBlockName;
    out += ";\n\n";
  }
  AppendSamplers(&out, interface);

  out += "struct FragmentIn {\n";
  out += std::string(Indent) + "@builtin(position) position: vec4<f32>,\n";
  for (size_t index = 0; index < interface.varyings.size(); ++index) {
    AppendVaryingField(&out, interface.varyings[index], index);
  }
  out += "};\n\n";

  out += "@fragment\nfn main(in: FragmentIn) -> @location(0) vec4<f32> {\n";
  out += std::string(Indent) + "var ";
  out += CUSTOM_COLOR_OUTPUT_NAME;
  out += " = vec4<f32>(0.0);\n";
  for (const auto& varying : interface.varyings) {
    out += Indent + std::string("let ") + varying.name + " = in." + varying.name + ";\n";
  }
  if (HasUniforms(interface, ShaderStage::Fragment)) {
    for (const auto& uniform : interface.uniforms) {
      if (uniform.stage == ShaderStage::Fragment) {
        out += Indent + std::string("let ") + uniform.name + " = fb." + uniform.name + ";\n";
      }
    }
  }
  std::string fragmentBody = body;
  ReplaceWord(&fragmentBody, "gl_FragCoord", "in.position");
  out += fragmentBody;
  out += std::string(Indent) + "return ";
  out += CUSTOM_COLOR_OUTPUT_NAME;
  out += ";\n}\n";
  return out;
}

}  // namespace tgfx
