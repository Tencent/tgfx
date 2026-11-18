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

#include "gpu/aot/chain/ChainBodyEmitter.h"
#include <unordered_map>
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {
namespace {

constexpr const char* ExecutorBeginMarker = "#pragma tgfx_composed_executor_begin";
constexpr const char* ExecutorEndMarker = "#pragma tgfx_composed_executor_end";

// The GLSL expression an input designator resolves to. The negative designators are the
// interpreter's fixed sources, spelled through the source functions the chain text defines (the
// build expands the macros per variant, so only the function names survive into the stored text);
// any other negative value (the encoder's "unused" -2 among them) feeds vec4(0.0), exactly what
// evalChainSlot's ternary chains produce. A non-negative designator names a result register, and
// one no earlier slot wrote is a defect the interpreter would read as an undefined register
// value — decline instead of emitting it.
bool ResolveInput(int designator, const std::unordered_map<int, std::string>& registers,
                  std::string* expression) {
  switch (designator) {
    case -1:
      *expression = "chainGeomColorSource()";
      return true;
    case -3:
      *expression = "chainUnitCoverageSource()";
      return true;
    case -4:
      *expression = "vec4(1.0)";
      return true;
    case -5:
      *expression = "chainOpaqueGeomColorSource()";
      return true;
    default:
      break;
  }
  if (designator < 0) {
    *expression = "vec4(0.0)";
    return true;
  }
  const auto found = registers.find(designator);
  if (found == registers.end()) {
    return false;
  }
  *expression = found->second;
  return true;
}

}  // namespace

std::string EmitComposedChainBodyGLSL(const AOTProgram& program, bool xpNeedsBlendPass) {
  std::unordered_map<int, std::string> registers = {};
  std::string body = {};
  int nextLocal = 0;
  for (size_t slotIndex = 0; slotIndex < program.slots.size(); ++slotIndex) {
    const auto& slot = program.slots[slotIndex];
    // A dead instruction (outRegister < 0) writes nothing any later slot reads, and the operators
    // are pure, so skipping its evaluation changes no output the epilogue consumes.
    if (slot.op == AOTChainOp::None || slot.outRegister < 0) {
      continue;
    }
    std::string in0 = {};
    std::string in1 = {};
    if (!ResolveInput(slot.in0, registers, &in0) || !ResolveInput(slot.in1, registers, &in1)) {
      return {};
    }
    const std::string dataAt =
        std::to_string(slotIndex * static_cast<size_t>(ChainISA::PoolSlotWords));
    const std::string local = "cg" + std::to_string(nextLocal);
    const std::string data = "cgData" + std::to_string(nextLocal);
    ++nextLocal;
    switch (slot.op) {
      case AOTChainOp::ConstColor:
        body += "  ConstColorData " + data + " = chainLoadConstColor(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyConstColor(" + data + ".color, " + in0 + ", " +
                std::to_string(slot.constColor.inputMode) + ");\n";
        break;
      case AOTChainOp::ColorMatrix:
        body += "  ColorMatrixData " + data + " = chainLoadColorMatrix(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyColorMatrix(" + in0 + ", " + data + ".linear, " +
                data + ".translate);\n";
        break;
      case AOTChainOp::Luma:
        body += "  LumaData " + data + " = chainLoadLuma(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyLuma(" + in0 + ", vec3(" + data + ".kr, " + data +
                ".kg, " + data + ".kb));\n";
        break;
      case AOTChainOp::AlphaThreshold:
        body += "  AlphaThresholdData " + data + " = chainLoadAlphaThreshold(" + dataAt + ");\n";
        body +=
            "  vec4 " + local + " = pmApplyAlphaThreshold(" + in0 + ", " + data + ".threshold);\n";
        break;
      case AOTChainOp::ColorSpaceXform:
        body += "  vec4 " + local + " = chainApplyColorSpaceXform(" + in0 + ");\n";
        break;
      case AOTChainOp::Blend:
        body += "  vec4 " + local + " = xpBlendColors(" + in0 + ", " + in1 + ", " +
                std::to_string(slot.blend.blendMode) + ");\n";
        if (slot.blend.multiplyInputAlpha) {
          body += "  " + local + " *= chainGeomColorSource().a;\n";
        }
        break;
      case AOTChainOp::Texture:
        body += "  vec4 " + local + " = chainEvalTextureSlot(" + std::to_string(slotIndex) + ", " +
                std::to_string(ChainSlotSelector(slot)) + ");\n";
        break;
      case AOTChainOp::TexModulate:
        body += "  vec4 " + local + " = chainEvalTexModulate(" + std::to_string(slotIndex) + ", " +
                std::to_string(ChainSlotSelector(slot)) + ", " + in0 + ");\n";
        break;
      case AOTChainOp::InputOpaque:
        body += "  vec4 " + local + " = vec4(" + in0 + ".rgb, 1.0);\n";
        break;
      case AOTChainOp::MulAlpha:
        body += "  vec4 " + local + " = " + in0 + " * " + in1 + ".a;\n";
        break;
      case AOTChainOp::AARectCoverage:
        body += "  AARectCoverageData " + data + " = chainLoadAARectCoverage(" + dataAt + ");\n";
        body += "  vec4 " + local + " = chainAARectCoverage(" + in0 + ", " + data + ".rect);\n";
        break;
      case AOTChainOp::LocalRectCoverage:
        body +=
            "  LocalRectCoverageData " + data + " = chainLoadLocalRectCoverage(" + dataAt + ");\n";
        body += "  vec4 " + local + " = " + in0 + " * vec4(analyticLocalRectCoverage(" + data +
                ".rect, " + data + ".deviceToLocal));\n";
        break;
      case AOTChainOp::RRectCoverage:
        body += "  RRectCoverageData " + data + " = chainLoadRRectCoverage(" + dataAt + ");\n";
        body += "  vec4 " + local + " = aotApplyRRectCoverage(" + in0 + ", " + data + ".rect, " +
                data + ".radiiX, " + data + ".radiiY, " + data + ".antiAlias, " + data +
                ".deviceToLocal);\n";
        break;
      case AOTChainOp::Gradient:
        // The gradient's parameters sit in the chain-wide block region, which the shared
        // evaluator addresses itself; the literal op word satisfies its Packed.x check.
        body += "  vec4 " + local + " = vec4(0.0);\n";
        body += "  evalGradientSlot(ivec4(" +
                std::to_string(static_cast<int>(AOTChainOp::Gradient)) + ", 0, 0, 0), " + in0 +
                ", " + local + ");\n";
        break;
      case AOTChainOp::PerlinNoise:
        body += "  vec4 " + local + " = vec4(0.0);\n";
        body += "  evalNoiseSlot(ivec4(" +
                std::to_string(static_cast<int>(AOTChainOp::PerlinNoise)) + ", 0, 0, 0), " +
                dataAt + ", " + local + ");\n";
        break;
      default:
        // An operator the emitter has not learned: decline so the caller keeps the interpreter.
        return {};
    }
    registers[slot.outRegister] = local;
    if (static_cast<int>(program.rootRegister) == slot.outRegister) {
      body += "  result = " + local + ";\n";
    }
    if (program.clipCoverageRegister == slot.outRegister) {
      body += "  clipRegister = " + local + ";\n";
    }
    if (program.coverageRootRegister == slot.outRegister) {
      body += "  coverageRootRegister = " + local + ";\n";
    }
  }
  if (xpNeedsBlendPass) {
    // The XferProcessor's advanced blend pass: the loop's extra iteration, spelled directly. The
    // mode stays the XPBlendMode uniform exactly as the interpreter reads it.
    body += "  chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);\n";
    body += "  xpBlended = xpBlendColors(chainXPSrc, chainXPDst, XPBlendMode);\n";
  }
  return body;
}

bool ComposeChainFragmentGLSL(const AOTProgram& program, bool xpNeedsBlendPass, std::string* code) {
  if (code == nullptr) {
    return false;
  }
  const auto begin = code->find(ExecutorBeginMarker);
  if (begin == std::string::npos) {
    return false;
  }
  const auto end = code->find(ExecutorEndMarker, begin);
  if (end == std::string::npos) {
    return false;
  }
  const auto endOfLine = code->find('\n', end);
  if (endOfLine == std::string::npos) {
    return false;
  }
  const std::string body = EmitComposedChainBodyGLSL(program, xpNeedsBlendPass);
  if (body.empty()) {
    return false;
  }
  code->replace(begin, endOfLine - begin, body);
  return true;
}

}  // namespace tgfx
