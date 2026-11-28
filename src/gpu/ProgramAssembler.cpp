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

#include "ProgramAssembler.h"
#include <set>
#include "ProgramBuilder.h"
#include "core/utils/Log.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/ir/ProgramBodyPrinter.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/shaders/ShaderLibraryBundle.h"

namespace tgfx {
ProgramAssembler::ProgramAssembler(ProgramBuilder* builder) : builder(builder) {
}

bool ProgramAssembler::assemble(const LanguageTraits& traits, std::string* vertexBody,
                                std::string* fragmentBody, std::string* libraryText) {
  builder->finalizeShaders();
  auto& ir = builder->programIRBuilder().IR();
  std::string validationError = {};
  if (!ValidateProgramIR(ir, &validationError)) {
    LOGE("ProgramAssembler: %s", validationError.c_str());
    return false;
  }
  // Names become print-only here: one pass rewrites every renameable value name so that a printed
  // reference and a printed definition can only match when they are the same value.
  ProgramBodyPrinter::Uniquify(ir);
  std::set<std::string> emittedFunctions = {};
  const bool libraryTraits = traits.language == ShaderLibraryLanguage::WGSL;
  if (libraryTraits) {
    ShaderLibraryBundle::Get().appendFunction(ShaderLibraryLanguage::WGSL, "__wgsl_support__",
                                              &emittedFunctions, libraryText);
  }
  for (const auto& expr : ir.exprs) {
    const char* symbol = nullptr;
    if (expr.kind == IRExprKind::TextureSample) {
      if (libraryTraits) {
        symbol = "tgfxTextureSample";
      } else {
        symbol = "tgfxTextureSample";
        if (expr.samplerType == SLType::Texture2DRectSampler) {
          symbol = "tgfxTextureSampleRect";
        } else if (expr.samplerType == SLType::TextureExternalSampler) {
          symbol = "tgfxTextureSampleExternal";
        }
      }
    } else if (expr.kind == IRExprKind::Call) {
      const auto* desc = FindOperator(expr.op);
      if (desc == nullptr || desc->glslSymbol == nullptr || desc->includes.empty()) {
        continue;
      }
      symbol = desc->glslSymbol;
    }
    if (symbol == nullptr) {
      continue;
    }
    if (libraryTraits) {
      ShaderLibraryBundle::Get().appendFunction(traits.language, symbol, &emittedFunctions,
                                                libraryText);
    } else {
      // Operators and texture readers belong to the fragment stage: their bodies may read
      // fragment-only builtins, so pulling them into the vertex shader would fail to compile.
      builder->fragmentShaderBuilder()->appendFunction(symbol);
    }
  }
  BodyPrintTraits bodyTraits = ProgramBodyPrinter::GLSLTraits();
  if (traits.language == ShaderLibraryLanguage::MSL) {
    bodyTraits = ProgramBodyPrinter::MSLTraits();
  } else if (libraryTraits) {
    bodyTraits = ProgramBodyPrinter::WGSLTraits();
  }
  *vertexBody = ProgramBodyPrinter::PrintVertex(ir, bodyTraits);
  *fragmentBody = ProgramBodyPrinter::PrintFragment(ir, bodyTraits);
  if (vertexBody->empty() || fragmentBody->empty()) {
    LOGE("ProgramAssembler: the stage bodies cannot be printed from the IR");
    return false;
  }
  const auto swizzle = builder->getProgramInfo()->getOutputSwizzle();
  if (swizzle != Swizzle::RGBA()) {
    *fragmentBody += std::string(CUSTOM_COLOR_OUTPUT_NAME) + " = " + CUSTOM_COLOR_OUTPUT_NAME +
                     "." + swizzle.c_str() + ";\n";
  }
  *vertexBody += "}\n";
  *fragmentBody += "}\n";
  return true;
}
}  // namespace tgfx
