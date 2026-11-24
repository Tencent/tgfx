/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "ShaderBuilder.h"
#include "ShaderVar.h"
#include "gpu/ir/CoordValue.h"

namespace tgfx {
class FragmentProcessor;
class ProgramIRBuilder;

static constexpr char CUSTOM_COLOR_OUTPUT_NAME[] = "tgfx_FragColor";

struct TextureReadResult {
  std::string name = {};
  ValueID value = INVALID_VALUE_ID;
};

/// One recorded stage-output write: the geometry stage's own color or coverage, or the plain xfer
/// formula that multiplies the color chain by the coverage chain. A bytecode route reads these to
/// learn where its composition starts and how it ends; the GLSL route prints the same text it
/// always printed.
struct FragmentSourceRecord {
  enum class Kind : uint8_t {
    /// dst = src, the geometry stage's color.
    Color,
    /// dst = src, or vec4(src) when the source is a scalar, the geometry stage's coverage.
    Coverage,
    /// dst = src * operand, the plain xfer formula.
    Combine,
    /// dst = src, a local alias the child emission introduces (no instruction).
    Alias,
    /// dst = src.xy / src.z, the perspective divide of a projective coordinate.
    Project,
    /// dst is the stage's final output: the operator folded the coverage itself, so the epilogue
    /// must not multiply it again.
    Root,
  };
  Kind kind = Kind::Color;
  std::string dst = {};
  /// The value stored (Color/Coverage), or the color operand (Combine).
  std::string src = {};
  /// The coverage operand of Combine.
  std::string operand = {};
  /// The coverage store wraps a scalar source in vec4(...).
  bool splat = false;
  /// The alias's declaration spelling ("vec4", "highp vec2"), so the text stays as emitted.
  std::string declaration = {};
  /// The record's position among every emission record of the stage.
  uint32_t order = 0;
};

class FragmentShaderBuilder : public ShaderBuilder {
 public:
  explicit FragmentShaderBuilder(ProgramBuilder* program);

  ShaderStage shaderStage() const override {
    return ShaderStage::Fragment;
  }

  virtual std::string dstColor() = 0;

  /**
   * One structured call: the output variable, the catalog function name, and the argument list,
   * each spelled as the builder names it (a uniform, a prior call's output, or a literal). The
   * GLSL builder renders it back to the exact text the inline emission produced; a bytecode
   * builder resolves the names into its own instruction forms instead.
   */
  virtual void appendCall(const std::string& outputVar, const std::string& functionName,
                          const std::vector<ValueRef>& args, const std::string& wrapType = "",
                          const std::string& expression = "") = 0;

  /**
   * One structured call whose result lands in a builder-named temporary, for the call sites that
   * need the value under a name (a later call's argument). Returns the temporary's name.
   */
  virtual std::string appendTempCall(const std::string& functionName,
                                     const std::vector<ValueRef>& args) = 0;

  /**
   * The sink a bytecode builder installs to collect the stage-output writes; nullptr keeps the
   * records off (the GLSL route). The records append in emission order.
   */
  void setSourceRecorder(std::vector<FragmentSourceRecord>* records) {
    sourceRecorder = records;
  }

  /**
   * Writes the stage's color output from the named value and records where it came from.
   */
  void storeColor(const std::string& outputVar, const std::string& srcName);

  /**
   * Writes the stage's coverage output from the named value and records where it came from; a
   * scalar source widens to vec4 at the store.
   */
  void storeCoverage(const std::string& outputVar, const std::string& srcName, bool splat);

  /**
   * Declares one local alias of a value (a child processor's input, a geometry's hoisted varying)
   * and records that the two names carry the same value.
   */
  void aliasValue(const std::string& dstName, const std::string& srcName,
                  const std::string& declaration);

  /**
   * Reads one sampler at the named coordinate into a local and records the read when the plan can
   * carry it. The typed value is filled when a ProgramIRBuilder is supplied.
   */
  TextureReadResult readTexture(SamplerHandle handle, const std::string& coordName,
                                const std::string& strictSubset, const std::string& drawSubset,
                                bool* recorded, ProgramIRBuilder* irBuilder = nullptr,
                                ValueID samplerValue = INVALID_VALUE_ID,
                                SLType coordType = SLType::Void,
                                ValueID strictSubsetValue = INVALID_VALUE_ID,
                                ValueID drawSubsetValue = INVALID_VALUE_ID);

  /**
   * Records that a call already stored the stage's final output and folded its coverage: the
   * epilogue must not combine again. The value must be a call's register.
   */
  void recordStageRoot(const std::string& outputVar);

  /**
   * Assigns one value to a name the stage already declared and records that the two carry the
   * same value.
   */
  void copyValue(const std::string& dstName, const std::string& srcName);

  /**
   * Writes the plain output formula the color chain and the coverage chain combine through, and
   * records the two operands.
   */
  void combineColorWithCoverage(const std::string& outputVar, const std::string& colorVar,
                                const std::string& coverageVar);

  void onBeforeChildProcEmitCode(const FragmentProcessor* child) const;

  void onAfterChildProcEmitCode() const;

  void declareCustomOutputColor();

  /**
   * Emits perspective division code for a texture coordinate variable if needed, and returns the
   * resulting 2D coordinate variable name.
   * If coordVar is Float3, generates "highp vec2 perspCoord2D = {coord}.xy / {coord}.z;" and
   * returns "perspCoord2D". If coordVar is Float2, returns the original variable name without
   * generating any code.
   * @param coordVar The input coordinate variable (Float2 or Float3)
   * @return The name of the 2D coordinate variable to use for texture sampling
   */
  std::string emitPerspTextCoord(const CoordValue& coordVar);

  std::string clampToSubsets(const std::string& coordName, const std::string& extraSubset,
                             const std::string& subset) override;

 protected:
  virtual std::string colorOutputName() = 0;

  void recordFragmentInstruction(FragmentInstruction instruction) {
    if (fragmentProgramRecorder != nullptr) {
      fragmentProgramRecorder->instructions.push_back(std::move(instruction));
    }
  }

  std::vector<FragmentSourceRecord>* sourceRecorder = nullptr;

  friend class ProgramBuilder;
};
}  // namespace tgfx
