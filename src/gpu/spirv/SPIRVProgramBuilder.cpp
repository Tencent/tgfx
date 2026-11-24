/////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "SPIRVProgramBuilder.h"
#include "SPIRVShaderBuilder.h"

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool BuildStage(const ProgramIR& program, ShaderStage stage, std::vector<uint32_t>* words,
                std::string* error) {
  SPIRVVertexShaderBuilder vertexBuilder = {};
  SPIRVFragmentShaderBuilder fragmentBuilder = {};
  SPIRVShaderBuilder* builder = stage == ShaderStage::Vertex
                                    ? static_cast<SPIRVShaderBuilder*>(&vertexBuilder)
                                    : static_cast<SPIRVShaderBuilder*>(&fragmentBuilder);
  if (!builder->Begin(error) || !builder->DeclareLocals(program, error)) {
    return false;
  }
  for (const auto& statement : program.statements) {
    switch (statement.kind) {
      case IRStatementKind::Assign:
        if (!builder->Assign(program, statement.dst, statement.src, error)) {
          return false;
        }
        break;
      case IRStatementKind::StoreOutput:
        if (!builder->StoreOutput(program, statement.src, error)) {
          return false;
        }
        break;
      case IRStatementKind::StoreVarying:
        if (stage != ShaderStage::Vertex) {
          return Fail(error, "a varying store was recorded in a fragment program");
        }
        if (!builder->StoreVarying(program, statement.dst, statement.src, error)) {
          return false;
        }
        break;
      case IRStatementKind::Call:
      case IRStatementKind::Return:
        break;
    }
  }
  *words = builder->Finish(error);
  return !words->empty();
}

}  // namespace

bool SPIRVProgramBuilder::Build(const ProgramIR& vertexProgram, const ProgramIR& fragmentProgram,
                                SPIRVProgramModules* modules, std::string* error) {
  if (modules == nullptr) {
    return Fail(error, "the SPIR-V program modules are null");
  }
  modules->vertex.clear();
  modules->fragment.clear();
  modules->layout = {};
  if (!BuildStage(vertexProgram, ShaderStage::Vertex, &modules->vertex, error) ||
      !BuildStage(fragmentProgram, ShaderStage::Fragment, &modules->fragment, error)) {
    return false;
  }
  SPIRVInterfaceLayout vertexLayout = {};
  SPIRVInterfaceLayout fragmentLayout = {};
  if (!vertexLayout.Build(vertexProgram.interface, error) ||
      !fragmentLayout.Build(fragmentProgram.interface, error) ||
      !modules->layout.Append(vertexLayout, error) ||
      !modules->layout.Append(fragmentLayout, error)) {
    return false;
  }
  return true;
}

}  // namespace tgfx
