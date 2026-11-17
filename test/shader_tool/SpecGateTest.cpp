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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The text-gate machinery had no test from the day it landed until the day its scanner was
// deleted; these tests pin every link of the restored chain: the pragma scan, the scalar-field
// filter, and the two dialects' wrapping (the GLSL family and the SPIRV-Cross HLSL), so a gate
// that would silently stop applying fails here instead of shipping ungated text.

#include <string>
#include <vector>
#include "compile/SpecCodegen.h"
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/spec/SpecGatePruner.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

SpecEntry Entry(const std::string& name, uint16_t firstId, uint16_t arraySize = 1) {
  SpecEntry entry = {};
  entry.name = name;
  entry.firstId = firstId;
  entry.arraySize = arraySize;
  return entry;
}

}  // namespace

TEST(SpecGateTest, ScanCollectsNameExpressionAndFunction) {
  Diagnostics diagnostics;
  const std::string source =
      "#version 450\n"
      "// #pragma tgfx_spec_gate Commented : true\n"
      "#pragma tgfx_spec_gate Noise : (SPEC_UsedOps() & (1 << OP_PERLIN_NOISE)) != 0\n"
      "vec4 evalNoiseSlot(int slot) {\n  return vec4(1.0);\n}\n";
  const auto gates = ScanGatePragmas(source, "Chain", &diagnostics);
  ASSERT_EQ(gates.size(), 1u);
  EXPECT_EQ(gates[0].name, "Noise");
  EXPECT_EQ(gates[0].expr, "(SPEC_UsedOps() & (1 << OP_PERLIN_NOISE)) != 0");
  EXPECT_EQ(gates[0].functionName, "evalNoiseSlot");
  EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(SpecGateTest, ScanDeduplicatesNamesAndIgnoresComments) {
  Diagnostics diagnostics;
  const std::string source =
      "#pragma tgfx_spec_gate First : SPEC_A() == 1\n"
      "int oneFn() { return 1; }\n"
      "#pragma tgfx_spec_gate First : SPEC_A() == 2\n"
      "int otherFn() { return 2; }\n";
  const auto gates = ScanGatePragmas(source, "Chain", &diagnostics);
  ASSERT_EQ(gates.size(), 1u);
  EXPECT_EQ(gates[0].expr, "SPEC_A() == 1");
  EXPECT_EQ(gates[0].functionName, "oneFn");
}

TEST(SpecGateTest, ScanReportsAGateWithoutAFunction) {
  Diagnostics diagnostics;
  const std::string source =
      "#pragma tgfx_spec_gate Dangling : SPEC_A() == 1\n"
      "// no function definition follows, only a comment\n";
  const auto gates = ScanGatePragmas(source, "Chain", &diagnostics);
  EXPECT_TRUE(gates.empty());
  ASSERT_EQ(diagnostics.records().size(), 1u);
  EXPECT_EQ(diagnostics.records()[0].code, DiagnosticCode::SpecGateUnmatched);
  EXPECT_EQ(diagnostics.records()[0].shader, "Chain");
}

TEST(SpecGateTest, ScanReportsAGateAtTheEndOfTheText) {
  Diagnostics diagnostics;
  const auto gates =
      ScanGatePragmas("#pragma tgfx_spec_gate Tail : SPEC_A() == 1", "Chain", &diagnostics);
  EXPECT_TRUE(gates.empty());
  EXPECT_EQ(diagnostics.records().size(), 1u);
  EXPECT_EQ(diagnostics.records()[0].code, DiagnosticCode::SpecGateUnmatched);
}

TEST(SpecGateTest, EvaluatableRequiresScalarFields) {
  const std::vector<SpecEntry> entries = {Entry("UsedOps", 7), Entry("SlotPacked", 2, 16)};
  EXPECT_TRUE(GateEvaluatable({"Noise", "SPEC_UsedOps() != 0", "evalNoiseSlot"}, entries));
  // An ivec4 array element cannot appear in a preprocessor condition.
  EXPECT_FALSE(GateEvaluatable({"Slots", "SPEC_SlotPacked() != 0", "evalSlots"}, entries));
  EXPECT_FALSE(GateEvaluatable({"Missing", "SPEC_Gone() != 0", "evalGone"}, entries));
  // The prefix must start an identifier: XSPEC_UsedOps is another field's spelling.
  EXPECT_TRUE(GateEvaluatable({"Boundary", "XSPEC_UsedOps() != 0", "evalBoundary"}, entries));
}

TEST(SpecGateTest, GlslWrapHangsTheHeaderOnTheVersionLine) {
  const std::string text = "#version 450\nvec4 evalNoise(int slot) {\n  return vec4(1.0);\n}\n";
  const auto gated = ApplyTextSpecGates(
      text, {{"Noise", "(SPEC_UsedOps() & 16) != 0", "evalNoise"}}, {Entry("UsedOps", 7)});
  EXPECT_TRUE(gated.error.empty());
  ASSERT_EQ(gated.wrapped, 1u);
  EXPECT_NE(gated.text.find("#define TGFX_GATE_Noise (TGFX_SPEC_0 == 0 || "
                            "((TGFX_SPEC_7 & 16) != 0))\n"),
            std::string::npos);
  // The signature stays outside the gate (the stub keeps it), so the directive lands between
  // the signature and the body's opening brace.
  EXPECT_NE(gated.text.find("vec4 evalNoise(int slot) \n#if TGFX_GATE_Noise\n{"),
            std::string::npos);
  EXPECT_NE(gated.text.find("#else\n{ return vec4(0.0); }\n#endif"), std::string::npos);
  // The generic form keeps the body: the master switch defaults to 0, so the gate opens.
  EXPECT_NE(gated.text.find("return vec4(1.0);"), std::string::npos);
}

TEST(SpecGateTest, GlslWrapRejectsAnUnknownField) {
  const auto gated = ApplyTextSpecGates("#version 450\nint f() { return 1; }\n",
                                        {{"G", "SPEC_Gone() != 0", "f"}}, {Entry("UsedOps", 7)});
  EXPECT_NE(gated.error.find("SPEC_Gone"), std::string::npos);
}

TEST(SpecGateTest, GlslWrapRejectsAnUnstubbableReturn) {
  const auto gated = ApplyTextSpecGates("#version 450\nmat3 m() { return mat3(1.0); }\n",
                                        {{"M", "SPEC_UsedOps() != 0", "m"}}, {Entry("UsedOps", 7)});
  EXPECT_NE(gated.error.find("no zero-value stub"), std::string::npos);
}

TEST(SpecGateTest, AWrapWithoutAMatchingFunctionLeavesTheTextAlone) {
  const std::string text = "#version 450\nint kept() { return 1; }\n";
  const auto gated =
      ApplyTextSpecGates(text, {{"G", "SPEC_UsedOps() != 0", "absent"}}, {Entry("UsedOps", 7)});
  EXPECT_TRUE(gated.error.empty());
  EXPECT_EQ(gated.wrapped, 0u);
  EXPECT_EQ(gated.text, text);
}

TEST(SpecGateTest, HlslWrapUsesTheSpirvCrossPrefixAndNoVersionLine) {
  const std::string text =
      "#include \"common.h\"\nfloat4 evalNoise(int slot) {\n"
      "  return float4(1.0, 1.0, 1.0, 1.0);\n}\n";
  const GateDialect dialect = {SpecContract::HlslConstantPrefix, true};
  const auto gated = ApplyTextSpecGates(text, {{"Noise", "SPEC_UsedOps() != 0", "evalNoise"}},
                                        {Entry("UsedOps", 7)}, dialect);
  EXPECT_TRUE(gated.error.empty());
  ASSERT_EQ(gated.wrapped, 1u);
  // The defines go first: the HLSL opens with includes, not a version directive.
  EXPECT_EQ(gated.text.find("#define TGFX_GATE_Noise"), 0u);
  EXPECT_NE(gated.text.find("(SPIRV_CROSS_CONSTANT_ID_0 == 0 || "
                            "(SPIRV_CROSS_CONSTANT_ID_7 != 0))"),
            std::string::npos);
  EXPECT_NE(gated.text.find("#else\n{ return float4(0.0, 0.0, 0.0, 0.0); }\n#endif"),
            std::string::npos);
}

namespace {

// The shape ApplyTextSpecGates emits, trimmed to the gate it wraps.
const char* GatedText =
    "#version 450\n"
    "#define TGFX_GATE_Noise (TGFX_SPEC_0 == 0 || ((TGFX_SPEC_9 & 32768) != 0))\n"
    "vec4 evalNoise(int slot)\n"
    "#if TGFX_GATE_Noise\n"
    "{\n  return vec4(1.0);\n}\n"
    "#else\n"
    "{ return vec4(0.0); }\n"
    "#endif\n"
    "int tail() { return 1; }\n";

}  // namespace

TEST(SpecGateTest, PrunerSplicesAClosedGateOut) {
  std::vector<uint32_t> values = std::vector<uint32_t>(10, 0);
  values[0] = 1;
  const auto pruned = ResolveSpecGates(GatedText, values, SpecContract::ConstantPrefix);
  EXPECT_EQ(pruned.find("return vec4(1.0);"), std::string::npos);
  EXPECT_NE(pruned.find("vec4 evalNoise(int slot)\n{ return vec4(0.0); }\n"), std::string::npos);
  EXPECT_EQ(pruned.find("#if "), std::string::npos);
  EXPECT_EQ(pruned.find("#define TGFX_GATE_"), std::string::npos);
  EXPECT_NE(pruned.find("int tail() { return 1; }"), std::string::npos);
}

TEST(SpecGateTest, PrunerKeepsAnOpenGateBody) {
  std::vector<uint32_t> values = std::vector<uint32_t>(10, 0);
  values[0] = 1;
  values[9] = 32768;
  const auto pruned = ResolveSpecGates(GatedText, values, SpecContract::ConstantPrefix);
  EXPECT_NE(pruned.find("return vec4(1.0);"), std::string::npos);
  EXPECT_EQ(pruned.find("return vec4(0.0);"), std::string::npos);
  EXPECT_EQ(pruned.find("#if "), std::string::npos);
}

TEST(SpecGateTest, PrunerLeavesATextWithoutGatesAlone) {
  const std::string text = "#version 450\nint f() { return 1; }\n";
  EXPECT_EQ(ResolveSpecGates(text, {1}, SpecContract::ConstantPrefix), text);
}

TEST(SpecGateTest, PrunerFailsClosedOnMalformedText) {
  std::vector<uint32_t> values = std::vector<uint32_t>(10, 0);
  values[0] = 1;
  // An unterminated gate, an unknown gate name, and a directive inside a span each return the
  // input unchanged: the caller serves exactly what it served before this pass existed.
  const std::string unterminated =
      "#version 450\n#define TGFX_GATE_Noise (TGFX_SPEC_0 == 0 || ((TGFX_SPEC_9 & 1) != 0))\n"
      "#if TGFX_GATE_Noise\n{ return vec4(0.0); }\n#else\n{ return vec4(1.0); }\n";
  EXPECT_EQ(ResolveSpecGates(unterminated, values, SpecContract::ConstantPrefix), unterminated);
  const std::string unknown =
      "#version 450\n#if TGFX_GATE_Missing\n{ return vec4(0.0); }\n#else\n{ return vec4(1.0); "
      "}\n#endif\n";
  EXPECT_EQ(ResolveSpecGates(unknown, values, SpecContract::ConstantPrefix), unknown);
  const std::string nested =
      "#version 450\n#define TGFX_GATE_Noise (TGFX_SPEC_0 == 0 || ((TGFX_SPEC_9 & 1) != 0))\n"
      "#if TGFX_GATE_Noise\n#if OTHER\n{ }\n#endif\n#else\n{ return vec4(1.0); }\n#endif\n";
  EXPECT_EQ(ResolveSpecGates(nested, values, SpecContract::ConstantPrefix), nested);
}

TEST(SpecGateTest, UncalledFunctionsAreRemovedAndCascade) {
  const std::string text =
      "int live() { return 1; }\n"
      "int orphanA() { return 2; }\n"
      "int orphanB() { return orphanA() + 1; }\n"
      "void main() { float c = float(live()); }\n";
  const auto pruned = RemoveUncalledFunctions(text);
  EXPECT_NE(pruned.find("int live()"), std::string::npos);
  EXPECT_EQ(pruned.find("orphanA"), std::string::npos);
  EXPECT_EQ(pruned.find("orphanB"), std::string::npos) << "the cascade must drop both";
  EXPECT_NE(pruned.find("void main()"), std::string::npos);
  EXPECT_EQ(RemoveUncalledFunctions("int f() { return 1; }\n"), "\n");
}

TEST(SpecGateTest, AnUnbalancedDefinitionLeavesTheTextAlone) {
  const std::string text = "int broken() { if (true) { return 1; }\n";
  EXPECT_EQ(RemoveUncalledFunctions(text), text);
}

TEST(SpecGateTest, PrunerRoundTripsTheToolsOwnGate) {
  // The strongest link: the tool emits the gate, the runtime parses what the tool emitted.
  const auto gated =
      ApplyTextSpecGates("#version 450\nvec4 f(int a) {\n  return vec4(1.0);\n}\n",
                         {{"ColorOps", "(SPEC_UsedOps() & 16) != 0", "f"}}, {Entry("UsedOps", 7)});
  ASSERT_TRUE(gated.error.empty());
  ASSERT_EQ(gated.wrapped, 1u);
  std::vector<uint32_t> values = std::vector<uint32_t>(8, 0);
  values[0] = 1;
  const auto closed = ResolveSpecGates(gated.text, values, SpecContract::ConstantPrefix);
  EXPECT_EQ(closed.find("return vec4(1.0);"), std::string::npos);
  EXPECT_NE(closed.find("return vec4(0.0);"), std::string::npos);
  values[7] = 16;
  const auto open = ResolveSpecGates(gated.text, values, SpecContract::ConstantPrefix);
  EXPECT_NE(open.find("return vec4(1.0);"), std::string::npos);
}

TEST(SpecGateTest, NormalizeAcceptsTheCanonicalForms) {
  const std::vector<SpecEntry> entries = {Entry("UsedOps", 7)};
  SpecGateInfo gate = {"Ops", "(SPEC_UsedOps() & ((1 << OP_LUMA) | (1 << OP_COLOR_MATRIX))) != 0",
                       "f", 0};
  ASSERT_TRUE(NormalizeGateCondition(&gate, entries));
  EXPECT_EQ(gate.mask, (1u << 1u) | (1u << 0u));
  EXPECT_EQ(gate.expr, "(SPEC_UsedOps() & 3) != 0");
  SpecGateInfo plain = {"Ops", "SPEC_UsedOps() != 0", "f", 0};
  ASSERT_TRUE(NormalizeGateCondition(&plain, entries));
  EXPECT_EQ(plain.mask, 0xFFFFFFFFu);
  // A range comparison, an unknown field and a zero mask are all rejected.
  SpecGateInfo range = {"Ops", "SPEC_UsedOps() > 2", "f", 0};
  EXPECT_FALSE(NormalizeGateCondition(&range, entries));
  SpecGateInfo missing = {"Ops", "(SPEC_Gone() & 1) != 0", "f", 0};
  EXPECT_FALSE(NormalizeGateCondition(&missing, entries));
  SpecGateInfo zero = {"Ops", "(SPEC_UsedOps() & 0) != 0", "f", 0};
  EXPECT_FALSE(NormalizeGateCondition(&zero, entries));
}

TEST(SpecGateTest, FeatureMasksFollowTheSlots) {
  AOTProgram program = {};
  AOTChainSlot blend = {};
  blend.op = AOTChainOp::Blend;
  blend.blend.blendMode = 5;  // tgfx::BlendMode::SrcIn
  AOTChainSlot gradient = {};
  gradient.op = AOTChainOp::Gradient;
  gradient.gradient.colorizerKind = 2;
  gradient.gradient.layoutType = 1;
  program.slots = {blend, gradient};
  const auto masks = ChainFeatureMasksOf(program);
  EXPECT_NE(masks.usedOps & (1u << static_cast<uint32_t>(AOTChainOp::Blend)), 0u);
  EXPECT_NE(masks.usedOps & (1u << static_cast<uint32_t>(AOTChainOp::Gradient)), 0u);
  EXPECT_EQ(masks.usedOps & (1u << static_cast<uint32_t>(AOTChainOp::PerlinNoise)), 0u);
  EXPECT_EQ(masks.blendModes, 1u << 5u);
  EXPECT_EQ(masks.gradientKinds, (1u << 2u) | (1u << 9u));
  EXPECT_EQ(masks.textureFlags, 0u);
  // The tiled and device-space facts live on the leaf records, not the slots.
  program.tiledLeaves.push_back({});
  EXPECT_EQ(ChainFeatureMasksOf(program).textureFlags, 0x2u);
}

TEST(SpecGateTest, StubOutReplacesTheBodies) {
  const std::string source = "vec4 a() { return vec4(1.0); }\nint b() { return 1; }\n";
  const auto stubbed = StubOutGatedFunctions(source, {{"A", "true", "a"}, {"B", "true", "b"}});
  EXPECT_NE(stubbed.find("vec4 a() { return vec4(0.0); }"), std::string::npos);
  EXPECT_NE(stubbed.find("int b() { return 0; }"), std::string::npos);
  EXPECT_EQ(stubbed.find("vec4(1.0)"), std::string::npos);
}

}  // namespace tgfx
