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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ChainOpsGLSL.h"
#include <cstring>
#include <string>
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {

namespace {

constexpr const char* ChainOpsMarker = "#pragma tgfx_chain_ops";

// The struct member and the pool reader of one field. Every field starts a fresh word, which is
// the same rule the C++ packer follows; a mat3 occupies three words as its columns.
struct ReaderEmitter {
  std::string members;
  std::string readers;
  int at = 0;

  void Field(float*, const char* name) {
    members += std::string("  float ") + name + ";\n";
    Push(name, ".x");
  }

  void Field(ChainISA::Float2*, const char* name) {
    members += std::string("  vec2 ") + name + ";\n";
    Push(name, ".xy");
  }

  void Field(ChainISA::Float3*, const char* name) {
    members += std::string("  vec3 ") + name + ";\n";
    Push(name, ".xyz");
  }

  void Field(ChainISA::Float4*, const char* name) {
    members += std::string("  vec4 ") + name + ";\n";
    Push(name, "");
  }

  void Field(ChainISA::Matrix3*, const char* name) {
    members += std::string("  mat3 ") + name + ";\n";
    readers += "  d." + std::string(name) + " = mat3(ChainData[at + " + std::to_string(at) +
               "].xyz, ChainData[at + " + std::to_string(at + 1) + "].xyz, ChainData[at + " +
               std::to_string(at + 2) + "].xyz);\n";
    at += 3;
  }

  void Field(ChainISA::Matrix4*, const char* name) {
    members += std::string("  mat4 ") + name + ";\n";
    readers += "  d." + std::string(name) + " = mat4(ChainData[at + " + std::to_string(at) +
               "], ChainData[at + " + std::to_string(at + 1) + "], ChainData[at + " +
               std::to_string(at + 2) + "], ChainData[at + " + std::to_string(at + 3) + "]);\n";
    at += 4;
  }

  void Field(ChainISA::Float4Array8*, const char* name) {
    members += std::string("  vec4 ") + name + "[8];\n";
    readers += "  for (int index = 0; index < 8; ++index) {\n    d." + std::string(name) +
               "[index] = ChainData[at + " + std::to_string(at) + " + index];\n  }\n";
    at += 8;
  }

  void Field(ChainISA::Int32*, const char* name) {
    members += std::string("  int ") + name + ";\n";
    readers += "  d." + std::string(name) + " = floatBitsToInt(ChainData[at + " +
               std::to_string(at) + "].x);\n";
    at += 1;
  }

 private:
  void Push(const char* name, const char* swizzle) {
    readers += "  d." + std::string(name) + " = ChainData[at + " + std::to_string(at) + "]" +
               swizzle + ";\n";
    at += 1;
  }
};

std::string GenerateChainOpsGLSL() {
  using namespace ChainISA;
  std::string out =
      "// Generated from contract/ChainOps.h: the chain data-pool layout and its readers.\n";
#define TGFX_EMIT_FIELD(type, name) emitter.Field(static_cast<type*>(nullptr), #name);
#define TGFX_EMIT_OP(name, dataFields)                                                  \
  {                                                                                     \
    ReaderEmitter emitter;                                                              \
    dataFields(TGFX_EMIT_FIELD)                                                         \
    if (!emitter.members.empty()) {                                                     \
      out += "struct " #name "Data {\n" + emitter.members + "};\n";                      \
      out += #name "Data chainLoad" #name "(int at) {\n  " #name "Data d;\n" +           \
             emitter.readers + "  return d;\n}\n";                                      \
    }                                                                                   \
  }
  TGFX_CHAIN_OP_FIELD_LISTS(TGFX_EMIT_OP)
#undef TGFX_EMIT_OP
#undef TGFX_EMIT_FIELD
  return out;
}

}  // namespace

std::string ExpandChainOpsPragma(const std::string& source) {
  if (source.find(ChainOpsMarker) == std::string::npos) {
    return source;
  }
  const auto generated = GenerateChainOpsGLSL();
  std::string result = source;
  size_t offset = 0;
  while ((offset = result.find(ChainOpsMarker, offset)) != std::string::npos) {
    result.replace(offset, std::strlen(ChainOpsMarker), generated);
    offset += generated.size();
  }
  return result;
}

}  // namespace tgfx
