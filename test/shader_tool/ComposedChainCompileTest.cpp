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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The composed executor's build-time self-check: the same emitter the runtime splices into a
// specialized text, run here over an assembled chain variant and compiled with the real shaderc.
// This is the static gate for the emitter's call marshalling — a wrong parameter group, a
// misspelled function name or a missing dependency fails this compile instead of surfacing as a
// runtime fallback or a pixel difference.

#include <string>
#include <vector>
#include "compile/ShaderCompiler.h"
#include "compile/StageCompiler.h"
#include "core/CapacityDefines.h"
#include "core/ShaderSources.h"
#include "emit/Assembler.h"
#include "emit/ChainOpsGLSL.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/ChainComposition.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

const Family& ChainFamily(uint16_t* familyIndex) {
  for (size_t index = 0; index < FamilyCount(); ++index) {
    if (std::string(Families()[index].name) == "Chain") {
      *familyIndex = static_cast<uint16_t>(index);
      return Families()[index];
    }
  }
  ADD_FAILURE() << "the Chain family is not in the table";
  *familyIndex = 0;
  return Families()[0];
}

// Assembles the first chain variant whose PROFILE dim is `profile` and whose Xfer's HAS_XP dim is
// at least `minHasXp`: every chain variant carries the executor pragmas, so any allowed one hosts
// the splice, and the profile picks which resource modules the text declares.
AssembledVariant AssembleChainVariant(uint8_t profile, uint8_t minHasXp) {
  uint16_t index = 0;
  const auto& family = ChainFamily(&index);
  for (const auto& variant : EnumerateVariants(family, index)) {
    const uint8_t xferSlot = static_cast<uint8_t>(2 + family.moduleCount);
    if (DimValueOf(family, variant, 2, 0) == profile &&
        DimValueOf(family, variant, xferSlot, 0) >= minHasXp) {
      return AssembleVariant(family, variant);
    }
  }
  return {};
}

// The assembled text carries module #includes and the chain-ops pragma, so the same expansion the
// build runs applies here: include expansion against the shader root, then the operator table.
// The executor pragmas only exist after this step — the splice operates on this text.
std::string ExpandedChainText(const AssembledVariant& assembled) {
  std::string expandError;
  auto fragText = ExpandStageIncludes(assembled.fragText, RealShaderDir, &expandError);
  if (fragText.empty()) {
    ADD_FAILURE() << expandError;
    return {};
  }
  return ExpandChainOpsPragma(fragText);
}

// The defines the build injects for the chain family: the variant's dim defines, the binding
// contract macros, the operator table and the slot capacities.
std::string CompilableText(const AssembledVariant& assembled, const std::string& expanded) {
  auto defines = assembled.fragDefines;
  for (const auto& contractDefine : BindingContractDefines()) {
    defines.push_back(contractDefine);
  }
  for (const auto& opDefine : Op::Defines()) {
    defines.push_back(opDefine);
  }
  for (const auto& capacityDefine : SlotCapacityDefines()) {
    defines.push_back(capacityDefine);
  }
  return PrependDefines(expanded, defines);
}

ChainCompositionSlot Slot(int op, int in0, int in1, int outRegister) {
  ChainCompositionSlot slot = {};
  slot.op = op;
  slot.in0 = in0;
  slot.in1 = in1;
  slot.outRegister = outRegister;
  return slot;
}

void MustCompile(const AssembledVariant& assembled, const std::string& expanded, const char* what) {
  std::string error;
  const auto spirv = CompileGLSL(CompilableText(assembled, expanded), ShaderStageType::Fragment,
                                 "ComposedChainTest", 0, false, &error);
  EXPECT_FALSE(spirv.empty()) << what << "\n" << error;
}

}  // namespace

TEST(ComposedChainCompileTest, PointwiseAndCoverageCompositionCompiles) {
  auto assembled = AssembleChainVariant(0, 0);
  ASSERT_FALSE(assembled.fragText.empty());
  auto expanded = ExpandedChainText(assembled);
  ASSERT_FALSE(expanded.empty());
  MustCompile(assembled, expanded, "the interpreter text must compile before the splice");

  ChainComposition composition = {};
  auto color = Slot(ChainISA::ConstColor, -4, -2, 0);
  color.inputMode = 1;
  auto blend = Slot(ChainISA::Blend, 0, -4, 1);
  blend.blendMode = 3;
  blend.multiplyInputAlpha = true;
  composition.slots = {color, Slot(ChainISA::ColorMatrix, 0, -2, 0), blend,
                       Slot(ChainISA::AARectCoverage, -3, -2, 2),
                       Slot(ChainISA::LocalRectCoverage, 2, -2, 2)};
  composition.rootRegister = 1;
  composition.coverageRootRegister = 2;
  ASSERT_TRUE(ComposeChainFragmentGLSL(composition, &expanded));
  MustCompile(assembled, expanded, "the composed pointwise body must compile");
}

TEST(ComposedChainCompileTest, GradientCompositionCompilesOnTheGradientProfile) {
  auto assembled = AssembleChainVariant(3, 0);
  ASSERT_FALSE(assembled.fragText.empty());
  auto expanded = ExpandedChainText(assembled);
  ASSERT_FALSE(expanded.empty());
  ChainComposition composition = {};
  composition.slots = {Slot(ChainISA::Gradient, -3, -2, 0)};
  composition.rootRegister = 0;
  ASSERT_TRUE(ComposeChainFragmentGLSL(composition, &expanded));
  MustCompile(assembled, expanded, "the composed gradient body must compile");
}

TEST(ComposedChainCompileTest, TextureAndXferPassCompositionCompiles) {
  auto assembled = AssembleChainVariant(2, 1);
  ASSERT_FALSE(assembled.fragText.empty());
  auto expanded = ExpandedChainText(assembled);
  ASSERT_FALSE(expanded.empty());
  ChainComposition composition = {};
  auto texture = Slot(ChainISA::Texture, -2, -2, 0);
  texture.selector = 1 | (1 << 20);
  auto modulate = Slot(ChainISA::TexModulate, -1, -2, 1);
  modulate.selector = 1 << 21;
  composition.slots = {texture, modulate};
  composition.rootRegister = 1;
  composition.xpNeedsBlendPass = true;
  ASSERT_TRUE(ComposeChainFragmentGLSL(composition, &expanded));
  MustCompile(assembled, expanded, "the composed texture and xfer-pass body must compile");
}

}  // namespace tgfx
