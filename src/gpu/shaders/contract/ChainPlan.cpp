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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/contract/ChainPlan.h"
#include <cstring>
#include <unordered_map>

namespace tgfx {
namespace {

// The std140 rules the parameter block follows: the alignment each type demands and the bytes it
// occupies. The declarations print in the same order the offsets accumulate, so the driver and the
// writer agree without an explicit-offset extension.
uint32_t Std140Alignment(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
    case FragmentType::Int:
      return 4;
    case FragmentType::Float2:
      return 8;
    default:
      return 16;
  }
}

uint32_t Std140Size(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
    case FragmentType::Int:
      return 4;
    case FragmentType::Float2:
      return 8;
    case FragmentType::Float3:
      return 12;
    case FragmentType::Float4:
    case FragmentType::Int4:
      return 16;
    case FragmentType::Matrix3:
      return 48;
    case FragmentType::Matrix4:
      return 64;
  }
  return 16;
}

// One operator's parameter table: the member name and type of each parameter the call passes.
// The program-level operators (gradient, noise, the color-space transform) read the prefix's
// named members directly and carry none.
struct PlanParamSpec {
  const char* name;
  FragmentType type;
};

const PlanParamSpec* ParamsOf(int op) {
  static const PlanParamSpec kColorMatrix[] = {{"linear", FragmentType::Matrix4},
                                               {"translate", FragmentType::Float4},
                                               {}};
  static const PlanParamSpec kLuma[] = {{"coefficients", FragmentType::Float3}, {}};
  static const PlanParamSpec kAlphaThreshold[] = {{"threshold", FragmentType::Float}, {}};
  static const PlanParamSpec kConstColor[] = {{"color", FragmentType::Float4}, {}};
  static const PlanParamSpec kAARect[] = {{"rect", FragmentType::Float4}, {}};
  static const PlanParamSpec kLocalRect[] = {{"rect", FragmentType::Float4},
                                             {"deviceToLocal", FragmentType::Matrix3},
                                             {}};
  static const PlanParamSpec kRRect[] = {
      {"rect", FragmentType::Float4},           {"radiiX", FragmentType::Float4},
      {"radiiY", FragmentType::Float4},         {"antiAlias", FragmentType::Float},
      {"deviceToLocal", FragmentType::Matrix3}, {}};
  switch (op) {
    case ChainISA::ColorMatrix:
      return kColorMatrix;
    case ChainISA::Luma:
      return kLuma;
    case ChainISA::AlphaThreshold:
      return kAlphaThreshold;
    case ChainISA::ConstColor:
      return kConstColor;
    case ChainISA::AARectCoverage:
      return kAARect;
    case ChainISA::LocalRectCoverage:
      return kLocalRect;
    case ChainISA::RRectCoverage:
      return kRRect;
    default:
      return nullptr;
  }
}

// The designators' fixed expressions, matching the source functions the assembler's wrapper
// declares (and the interpreter's own macros).
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

ChainPlan BuildChainPlan(const ChainComposition& composition) {
  ChainPlan plan = {};
  std::unordered_map<int, std::string> registers = {};
  uint32_t cursor = 0;
  int nextLocal = 0;
  for (size_t slotIndex = 0; slotIndex < composition.slots.size(); ++slotIndex) {
    const auto& slot = composition.slots[slotIndex];
    // A dead instruction writes nothing any later slot reads; skipping it changes no output.
    if (slot.op == ChainISA::None || slot.outRegister < 0) {
      continue;
    }
    std::string in0;
    std::string in1;
    if (!ResolveInput(slot.in0, registers, &in0) || !ResolveInput(slot.in1, registers, &in1)) {
      return {};
    }
    const auto* def = FindFragment(slot.op);
    if (def == nullptr) {
      return {};
    }
    PlanCall call = {};
    call.op = slot.op;
    call.slot = static_cast<int>(slotIndex);
    call.out = "cg" + std::to_string(nextLocal);
    ++nextLocal;
    // The slot's parameter members, declared as they are marshalled: the interface is the
    // emission's closure.
    auto member = [&](const char* param) -> std::string {
      PlanParam out = {};
      out.op = slot.op;
      out.slot = static_cast<int>(slotIndex);
      out.param = param;
      for (const auto* spec = ParamsOf(slot.op); spec != nullptr && spec->name != nullptr; ++spec) {
        if (std::strcmp(spec->name, param) == 0) {
          out.type = spec->type;
          break;
        }
      }
      out.member = std::string(def->functionName) + "_" + std::to_string(call.slot) + "_" + param;
      out.offset = (cursor + Std140Alignment(out.type) - 1) & ~(Std140Alignment(out.type) - 1);
      out.size = Std140Size(out.type);
      cursor = out.offset + out.size;
      plan.params.push_back(out);
      return out.member;
    };
    switch (slot.op) {
      case ChainISA::ConstColor:
        call.args = {member("color"), in0, std::to_string(slot.inputMode)};
        break;
      case ChainISA::ColorMatrix:
        call.args = {in0, member("linear"), member("translate")};
        break;
      case ChainISA::Luma:
        call.args = {in0, member("coefficients")};
        break;
      case ChainISA::AlphaThreshold:
        call.args = {in0, member("threshold")};
        break;
      case ChainISA::ColorSpaceXform:
        call.args = {in0};
        break;
      case ChainISA::Blend:
        call.args = {in0, in1, std::to_string(slot.blendMode)};
        if (slot.multiplyInputAlpha) {
          call.post = call.out + " *= chainGeomColorSource().a;";
        }
        break;
      case ChainISA::Texture:
        call.args = {"chainLeafTex[" + std::to_string(slotIndex) + "]", "chainGeomColorSource()",
                     "chainUnitCoverageSource()", std::to_string(slot.selector & 0x1F)};
        plan.hasTexture = true;
        plan.leafVaryings.insert(static_cast<int>(slotIndex));
        break;
      case ChainISA::TexModulate: {
        const int srcSlot = (slot.selector >> 16) & 0xF;
        call.args = {"chainLeafTex[" + std::to_string(srcSlot) + "]", in0,
                     std::to_string(slot.selector & 1)};
        plan.hasTexture = true;
        break;
      }
      case ChainISA::InputOpaque:
        call.args = {in0};
        break;
      case ChainISA::MulAlpha:
        call.args = {in0, in1};
        break;
      case ChainISA::AARectCoverage:
        call.args = {in0, member("rect"), "gl_FragCoord.xy"};
        break;
      case ChainISA::LocalRectCoverage:
        call.args = {in0, member("rect"), member("deviceToLocal"), "gl_FragCoord.xy"};
        break;
      case ChainISA::RRectCoverage:
        call.args = {in0,
                     member("rect"),
                     member("radiiX"),
                     member("radiiY"),
                     member("antiAlias"),
                     member("deviceToLocal"),
                     "gl_FragCoord.xy"};
        break;
      case ChainISA::Gradient:
        call.args = {in0};
        plan.needsSourceCoords = true;
        break;
      case ChainISA::PerlinNoise:
        call.args = {};
        plan.needsSourceCoords = true;
        break;
      default:
        return {};
    }
    if (composition.rootRegister == slot.outRegister) {
      call.sink = "result";
    } else if (composition.clipCoverageRegister == slot.outRegister) {
      call.sink = "clipRegister";
    } else if (composition.coverageRootRegister == slot.outRegister) {
      call.sink = "coverageRootRegister";
    }
    plan.calls.push_back(std::move(call));
    plan.usedOps.insert(slot.op);
    registers[slot.outRegister] = plan.calls.back().out;
  }
  plan.paramBlockSize = cursor;
  plan.xpNeedsBlendPass = composition.xpNeedsBlendPass;
  return plan;
}

}  // namespace tgfx
