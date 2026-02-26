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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ShaderTranslator.h"
#include <regex>
#include <sstream>
#include <vector>
#include "core/utils/Log.h"

namespace tgfx {

// Structure to hold parsed attribute info
struct AttributeInfo {
  std::string glslType = "";
  std::string name = "";
};

// Convert GLSL type to WGSL type
static std::string GlslTypeToWgsl(const std::string& glslType) {
  if (glslType == "float") {
    return "f32";
  }
  if (glslType == "vec2") {
    return "vec2f";
  }
  if (glslType == "vec3") {
    return "vec3f";
  }
  if (glslType == "vec4") {
    return "vec4f";
  }
  if (glslType == "int") {
    return "i32";
  }
  if (glslType == "ivec2") {
    return "vec2i";
  }
  if (glslType == "ivec3") {
    return "vec3i";
  }
  if (glslType == "ivec4") {
    return "vec4i";
  }
  if (glslType == "mat2") {
    return "mat2x2<f32>";
  }
  if (glslType == "mat3") {
    return "mat3x3<f32>";
  }
  if (glslType == "mat4") {
    return "mat4x4<f32>";
  }
  return "f32";  // Default fallback
}

// Parse vertex attributes from GLSL code
static std::vector<AttributeInfo> ParseGlslAttributes(const std::string& glslCode) {
  std::vector<AttributeInfo> attributes;

  // Match patterns like: "in vec2 aPosition;" or "in float inCoverage;"
  // Also handles highp/mediump/lowp precision qualifiers
  std::regex attrRegex(R"(in\s+(?:highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");

  auto begin = std::sregex_iterator(glslCode.begin(), glslCode.end(), attrRegex);
  auto end = std::sregex_iterator();

  for (auto it = begin; it != end; ++it) {
    AttributeInfo attr;
    attr.glslType = (*it)[1].str();
    attr.name = (*it)[2].str();
    attributes.push_back(attr);
  }

  return attributes;
}

// Generate test WGSL vertex shader that matches the GLSL attributes
// Note: This test shader does NOT declare any uniform bindings - they must match exactly
// with what the pipeline layout expects (from descriptor.layout.uniformBlocks).
static std::string GenerateTestVertexShader(const std::string& glslCode) {
  auto attributes = ParseGlslAttributes(glslCode);

  std::ostringstream ss;

  // Generate VertexInput struct - only vertex attributes, no uniforms
  ss << "struct VertexInput {\n";
  int location = 0;
  for (const auto& attr : attributes) {
    ss << "  @location(" << location << ") " << attr.name << ": " << GlslTypeToWgsl(attr.glslType)
       << ",\n";
    location++;
  }
  ss << "};\n\n";

  // Generate VertexOutput struct
  ss << "struct VertexOutput {\n";
  ss << "  @builtin(position) position: vec4f,\n";
  ss << "};\n\n";

  // Generate main function
  ss << "@vertex fn main(input: VertexInput) -> VertexOutput {\n";
  ss << "  var output: VertexOutput;\n";

  // Find position attribute and use it
  bool foundPosition = false;
  for (const auto& attr : attributes) {
    if (attr.name.find("osition") != std::string::npos ||
        attr.name.find("Position") != std::string::npos) {
      if (attr.glslType == "vec2") {
        ss << "  output.position = vec4f(input." << attr.name << ", 0.0, 1.0);\n";
      } else if (attr.glslType == "vec3") {
        ss << "  output.position = vec4f(input." << attr.name << ", 1.0);\n";
      } else if (attr.glslType == "vec4") {
        ss << "  output.position = input." << attr.name << ";\n";
      } else {
        ss << "  output.position = vec4f(0.0, 0.0, 0.0, 1.0);\n";
      }
      foundPosition = true;
      break;
    }
  }
  if (!foundPosition && !attributes.empty()) {
    const auto& firstAttr = attributes[0];
    if (firstAttr.glslType == "vec2") {
      ss << "  output.position = vec4f(input." << firstAttr.name << ", 0.0, 1.0);\n";
    } else {
      ss << "  output.position = vec4f(0.0, 0.0, 0.0, 1.0);\n";
    }
  }

  ss << "  return output;\n";
  ss << "}\n";

  return ss.str();
}

// Generate test WGSL fragment shader - outputs solid red color
// Note: This test shader does NOT declare any uniform or sampler bindings.
static std::string GenerateTestFragmentShader(const std::string& /*glslCode*/) {
  std::ostringstream ss;

  // Generate main function - just output solid red
  ss << "@fragment fn main() -> @location(0) vec4f {\n";
  ss << "  return vec4f(1.0, 0.0, 0.0, 1.0);\n";
  ss << "}\n";

  return ss.str();
}

std::string TranslateGLSLToWGSL(const std::string& glslCode, ShaderStage stage) {
  // TODO: Implement GLSL to WGSL translation using Tint.
  // For now, generate test shaders that render solid red to verify WebGPU pipeline.
  if (stage == ShaderStage::Vertex) {
    auto wgsl = GenerateTestVertexShader(glslCode);
    printf("Generated WGSL vertex shader:\n%s\n", wgsl.c_str());
    return wgsl;
  }
  auto wgsl = GenerateTestFragmentShader(glslCode);
  printf("Generated WGSL fragment shader:\n%s\n", wgsl.c_str());
  return wgsl;
}

}  // namespace tgfx
