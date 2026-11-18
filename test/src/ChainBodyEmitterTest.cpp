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

// The composed executor replaces the interpreter loop with per-slot locals wired by the decoded
// program. These tests pin the wiring rules the GLSL relies on: register reuse resolves to the
// producing local, the fixed negative designators keep their macros, the three epilogue registers
// are assigned at write time, and everything the emitter does not cover declines to an empty body
// so the caller keeps the interpreter text.

#include <string>
#include "base/TGFXTest.h"
#include "gpu/aot/chain/ChainBodyEmitter.h"
#include "gpu/aot/ir/Program.h"

namespace tgfx {
namespace {

AOTChainSlot MakeSlot(AOTChainOp op, int in0, int in1, int outRegister) {
  AOTChainSlot slot = {};
  slot.op = op;
  slot.in0 = in0;
  slot.in1 = in1;
  slot.outRegister = outRegister;
  return slot;
}

AOTProgram MakeProgram(std::vector<AOTChainSlot> slots, size_t rootRegister) {
  AOTProgram program = {};
  program.slots = std::move(slots);
  program.rootRegister = rootRegister;
  return program;
}

}  // namespace

TGFX_TEST(ChainBodyEmitterTest, WiresRegisterReuseToTheProducingLocal) {
  auto color = MakeSlot(AOTChainOp::ConstColor, -4, -2, 0);
  color.constColor.inputMode = 1;
  // The matrix reuses register 0: its input must read the const color's local, not a register
  // array, and the root assignment lands on the matrix's own local.
  AOTProgram program = MakeProgram({color, MakeSlot(AOTChainOp::ColorMatrix, 0, -2, 0)}, 0);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(body.find("pmApplyConstColor(cgData0.color, vec4(1.0), 1)"), std::string::npos);
  EXPECT_NE(body.find("pmApplyColorMatrix(cg0, cgData1.linear, cgData1.translate)"),
            std::string::npos);
  EXPECT_NE(body.find("result = cg1;"), std::string::npos);
  EXPECT_NE(body.find("chainLoadConstColor(0)"), std::string::npos);
  EXPECT_NE(body.find("chainLoadColorMatrix(7)"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, EmitsTheFixedDesignatorSourceFunctions) {
  AOTProgram program = MakeProgram(
      {MakeSlot(AOTChainOp::InputOpaque, -1, -2, 0), MakeSlot(AOTChainOp::MulAlpha, 0, -3, 1)}, 1);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(body.find("vec4 cg0 = vec4(chainGeomColorSource().rgb, 1.0);"), std::string::npos);
  EXPECT_NE(body.find("vec4 cg1 = cg0 * chainUnitCoverageSource().a;"), std::string::npos);
  EXPECT_NE(body.find("result = cg1;"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, BlendEmitsModeAndInputAlphaEpilogue) {
  auto blend = MakeSlot(AOTChainOp::Blend, 0, -4, 1);
  blend.blend.blendMode = 3;
  blend.blend.multiplyInputAlpha = true;
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ConstColor, -4, -2, 0), blend}, 1);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(body.find("xpBlendColors(cg0, vec4(1.0), 3)"), std::string::npos);
  EXPECT_NE(body.find("cg1 *= chainGeomColorSource().a;"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, TextureAndTexModulateCarryTheSelectorLiteral) {
  auto texture = MakeSlot(AOTChainOp::Texture, -2, -2, 0);
  texture.textureModulate = 1;
  auto modulate = MakeSlot(AOTChainOp::TexModulate, -1, -2, 1);
  modulate.texModulateSourceSlot = 0;
  AOTProgram program = MakeProgram({texture, modulate}, 1);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  // Texture slot 0 with bit 0 set and register 0: selector = 1 | (0 + 1) << 20.
  EXPECT_NE(body.find("chainEvalTextureSlot(0, " + std::to_string(1 | (1 << 20)) + ")"),
            std::string::npos);
  // TexModulate slot 1 reading source slot 0: selector bits 16-19 carry the 0, register 1.
  EXPECT_NE(
      body.find("chainEvalTexModulate(1, " + std::to_string(1 << 21) + ", chainGeomColorSource())"),
      std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, CoverageOpsAssignTheEpilogueRegisters) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::AARectCoverage, -3, -2, 0),
                                    MakeSlot(AOTChainOp::LocalRectCoverage, 0, -2, 1),
                                    MakeSlot(AOTChainOp::RRectCoverage, -3, -2, 2)},
                                   1);
  program.coverageRootRegister = 2;
  program.clipCoverageRegister = 0;
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(body.find("chainAARectCoverage(chainUnitCoverageSource(), cgData0.rect)"),
            std::string::npos);
  EXPECT_NE(body.find("clipRegister = cg0;"), std::string::npos);
  EXPECT_NE(body.find("result = cg1;"), std::string::npos);
  EXPECT_NE(body.find("coverageRootRegister = cg2;"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, GradientAndNoiseCallTheSharedEvaluators) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::Gradient, -3, -2, 0)}, 0);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(
      body.find("evalGradientSlot(ivec4(" + std::to_string(static_cast<int>(AOTChainOp::Gradient)) +
                ", 0, 0, 0), chainUnitCoverageSource(), cg0)"),
      std::string::npos);
  EXPECT_NE(body.find("result = cg0;"), std::string::npos);
  AOTProgram noise = MakeProgram({MakeSlot(AOTChainOp::PerlinNoise, -2, -2, 0)}, 0);
  const std::string noiseBody = EmitComposedChainBodyGLSL(noise, false);
  EXPECT_NE(noiseBody.find("evalNoiseSlot(ivec4(" +
                           std::to_string(static_cast<int>(AOTChainOp::PerlinNoise)) +
                           ", 0, 0, 0), 0, cg0)"),
            std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, UnwrittenRegisterInputDeclines) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ColorMatrix, 5, -2, 0)}, 0);
  EXPECT_TRUE(EmitComposedChainBodyGLSL(program, false).empty());
}

TGFX_TEST(ChainBodyEmitterTest, AdvancedXferBlendPassEmitsTheXPTail) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ConstColor, -4, -2, 0)}, 0);
  const std::string body = EmitComposedChainBodyGLSL(program, true);
  EXPECT_NE(body.find("chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);"),
            std::string::npos);
  EXPECT_NE(body.find("xpBlended = xpBlendColors(chainXPSrc, chainXPDst, XPBlendMode);"),
            std::string::npos);
  EXPECT_NE(body.find("result = cg0;"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, DeadSlotsAreSkipped) {
  AOTProgram program = MakeProgram(
      {MakeSlot(AOTChainOp::ConstColor, -4, -2, -1), MakeSlot(AOTChainOp::ConstColor, -4, -2, 0)},
      0);
  const std::string body = EmitComposedChainBodyGLSL(program, false);
  EXPECT_NE(body.find("cg0"), std::string::npos);
  EXPECT_NE(body.find("chainLoadConstColor(7)"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, SpliceReplacesTheMarkedRegionAndKeepsTheRest) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ConstColor, -4, -2, 0)}, 0);
  std::string code =
      "void chainMain() {\n  before\n#pragma tgfx_composed_executor_begin\n  loop();\n"
      "#pragma tgfx_composed_executor_end\n  after\n}\n";
  EXPECT_TRUE(ComposeChainFragmentGLSL(program, false, &code));
  EXPECT_NE(code.find("before"), std::string::npos);
  EXPECT_NE(code.find("after"), std::string::npos);
  EXPECT_NE(code.find("pmApplyConstColor"), std::string::npos);
  EXPECT_EQ(code.find("loop()"), std::string::npos);
  EXPECT_EQ(code.find("tgfx_composed_executor_begin"), std::string::npos);
  EXPECT_EQ(code.find("tgfx_composed_executor_end"), std::string::npos);
}

TGFX_TEST(ChainBodyEmitterTest, SpliceWithoutMarkersLeavesTheTextUntouched) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ConstColor, -4, -2, 0)}, 0);
  const std::string original = "void chainMain() { loop(); }\n";
  std::string code = original;
  EXPECT_FALSE(ComposeChainFragmentGLSL(program, false, &code));
  EXPECT_EQ(code, original);
}

TGFX_TEST(ChainBodyEmitterTest, SpliceOfAnUnsupportedProgramLeavesTheTextUntouched) {
  AOTProgram program = MakeProgram({MakeSlot(AOTChainOp::ColorMatrix, 5, -2, 0)}, 0);
  const std::string original =
      "#pragma tgfx_composed_executor_begin\nloop();\n#pragma tgfx_composed_executor_end\n";
  std::string code = original;
  EXPECT_FALSE(ComposeChainFragmentGLSL(program, false, &code));
  EXPECT_EQ(code, original);
}

}  // namespace tgfx
