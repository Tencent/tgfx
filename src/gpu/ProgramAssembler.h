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

#pragma once

#include <string>
#include "gpu/shaders/contract/ShaderLibraryBundleFormat.h"

namespace tgfx {
class ProgramBuilder;

/**
 * The language-specific facts the assembly needs. Every backend differs only by these fields: the
 * IR, the operator manifest and the binding layout stay shared.
 */
struct LanguageTraits {
  ShaderLibraryLanguage language = ShaderLibraryLanguage::GLSL;
  /// WGSL assembles its operator sources into one support text the stage shell embeds, instead of
  /// appending them to the builder's function section.
  bool pullsLibraryText = false;
};

constexpr LanguageTraits GLSL_Language = {ShaderLibraryLanguage::GLSL, false};
constexpr LanguageTraits MSL_Language = {ShaderLibraryLanguage::MSL, false};
constexpr LanguageTraits WGSL_Language = {ShaderLibraryLanguage::WGSL, true};
constexpr LanguageTraits HLSL_Language = {ShaderLibraryLanguage::HLSL, false};
constexpr LanguageTraits SPIRV_Language = {ShaderLibraryLanguage::SPIRV, false};

/**
 * The one path from ProgramIR to the two stage bodies: registers the program interface from the
 * handlers, validates the IR, makes the printable names unique, pulls the operator and reader
 * sources the bodies call, and prints the bodies in the language's spelling with the output
 * epilogue. A backend owns only the stage shell and the module/pipeline creation.
 */
class ProgramAssembler {
 public:
  explicit ProgramAssembler(ProgramBuilder* builder);

  /**
   * Assembles both stage bodies. libraryText receives the WGSL support text when the traits ask
   * for it, and stays empty otherwise. Returns false when the IR cannot be validated or printed.
   */
  bool assemble(const LanguageTraits& traits, std::string* vertexBody, std::string* fragmentBody,
                std::string* libraryText);

 private:
  ProgramBuilder* builder = nullptr;
};
}  // namespace tgfx
