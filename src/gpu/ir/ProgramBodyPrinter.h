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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include "gpu/ir/ProgramIR.h"
#include "gpu/shaders/contract/ShaderLibraryBundleFormat.h"

namespace tgfx {

/// The per-language facts a body needs beyond the value types: where the stage output lands, how a
/// discard is spelled, and whether the clip-space y is flipped at the store. Everything else about
/// a printed body (statements, declarations, identifier checks) is the same for every language.
struct BodyPrintTraits {
  ShaderLibraryLanguage language = ShaderLibraryLanguage::GLSL;
  /// The lvalue the vertex position is stored into, or nullptr when the language keeps the
  /// position under its IR name.
  const char* positionTarget = nullptr;
  /// The statement a failed coverage test emits.
  const char* discardStatement = "discard;";
  /// MSL's clip space points the other way from the RTAdjust mapping the GL path feeds.
  bool flipPositionY = false;
};

/// Turns a stage's ProgramIR statements into the stage body text. The stage's own printer owns the
/// interface declarations and the epilogue; this prints only what the processors emitted.
class ProgramBodyPrinter {
 public:
  /// The GLSL spellings: the position keeps its IR name (it is the builtin), the discard is the
  /// GLSL statement and no y flip happens.
  static BodyPrintTraits GLSLTraits() {
    return {ShaderLibraryLanguage::GLSL, nullptr, "discard;", false};
  }

  /// The MSL spellings (see BodyPrintTraits).
  static BodyPrintTraits MSLTraits() {
    return {ShaderLibraryLanguage::MSL, "out.position", "discard_fragment();", true};
  }

  /// The WGSL spellings: WebGPU's clip space points the same way MSL's does, and the discard is
  /// the WGSL statement.
  static BodyPrintTraits WGSLTraits() {
    return {ShaderLibraryLanguage::WGSL, "out.position", "discard;", true};
  }

  /// Renames the printable values in place: two emitters that named a value alike (or left it
  /// nameless) get stable generated names, so a printed reference and a printed definition can
  /// only match when they are the same value. Interface-owned names and builtin outputs keep
  /// theirs.
  static void Uniquify(ProgramIR& program);

  /// Prints the fragment body from the stage's statements. Returns an empty string when a
  /// statement cannot be printed, so the caller can report the gap.
  static std::string PrintFragment(const ProgramIR& program, const BodyPrintTraits& traits);

  /// Prints the vertex body from the stage's vertex statements under the same rules.
  static std::string PrintVertex(const ProgramIR& program, const BodyPrintTraits& traits);
};

}  // namespace tgfx
