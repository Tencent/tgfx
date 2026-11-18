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

#include "gpu/shaders/contract/ChainComposition.h"
#include <unordered_map>
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {
namespace {

constexpr const char* ExecutorBeginPragma = "#pragma tgfx_composed_executor_begin";
constexpr const char* ExecutorEndPragma = "#pragma tgfx_composed_executor_end";

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

std::string EmitComposedChainBodyGLSL(const ChainComposition& composition, bool pureOps) {
  std::unordered_map<int, std::string> registers = {};
  std::string body = {};
  int nextLocal = 0;
  for (size_t slotIndex = 0; slotIndex < composition.slots.size(); ++slotIndex) {
    const auto& slot = composition.slots[slotIndex];
    // A dead instruction (outRegister < 0) writes nothing any later slot reads, and the operators
    // are pure, so skipping its evaluation changes no output the epilogue consumes.
    if (slot.op == ChainISA::None || slot.outRegister < 0) {
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
      case ChainISA::ConstColor:
        body += "  ConstColorData " + data + " = chainLoadConstColor(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyConstColor(" + data + ".color, " + in0 + ", " +
                std::to_string(slot.inputMode) + ");\n";
        break;
      case ChainISA::ColorMatrix:
        body += "  ColorMatrixData " + data + " = chainLoadColorMatrix(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyColorMatrix(" + in0 + ", " + data + ".linear, " +
                data + ".translate);\n";
        break;
      case ChainISA::Luma:
        body += "  LumaData " + data + " = chainLoadLuma(" + dataAt + ");\n";
        body += "  vec4 " + local + " = pmApplyLuma(" + in0 + ", vec3(" + data + ".kr, " + data +
                ".kg, " + data + ".kb));\n";
        break;
      case ChainISA::AlphaThreshold:
        body += "  AlphaThresholdData " + data + " = chainLoadAlphaThreshold(" + dataAt + ");\n";
        body +=
            "  vec4 " + local + " = pmApplyAlphaThreshold(" + in0 + ", " + data + ".threshold);\n";
        break;
      case ChainISA::ColorSpaceXform:
        body += "  vec4 " + local + " = chainApplyColorSpaceXform(" + in0 + ");\n";
        break;
      case ChainISA::Blend:
        body += "  vec4 " + local + " = xpBlendColors(" + in0 + ", " + in1 + ", " +
                std::to_string(slot.blendMode) + ");\n";
        if (slot.multiplyInputAlpha) {
          body += "  " + local + " *= chainGeomColorSource().a;\n";
        }
        break;
      case ChainISA::Texture:
        if (pureOps) {
          // The assembled text owns the prefetch: the leaf sample is already in chainLeafTex.
          body += "  vec4 " + local + " = tgfxOpTexture(chainLeafTex[" + std::to_string(slotIndex) +
                  "], chainGeomColorSource(), chainUnitCoverageSource(), " +
                  std::to_string(slot.selector & 0x1F) + ");\n";
        } else {
          body += "  vec4 " + local + " = chainEvalTextureSlot(" + std::to_string(slotIndex) +
                  ", " + std::to_string(slot.selector) + ");\n";
        }
        break;
      case ChainISA::TexModulate:
        if (pureOps) {
          const int srcSlot = (slot.selector >> 16) & 0xF;
          body += "  vec4 " + local + " = tgfxOpTexModulate(chainLeafTex[" +
                  std::to_string(srcSlot) + "], " + in0 + ", " + std::to_string(slot.selector & 1) +
                  ");\n";
        } else {
          body += "  vec4 " + local + " = chainEvalTexModulate(" + std::to_string(slotIndex) +
                  ", " + std::to_string(slot.selector) + ", " + in0 + ");\n";
        }
        break;
      case ChainISA::InputOpaque:
        body += "  vec4 " + local + " = vec4(" + in0 + ".rgb, 1.0);\n";
        break;
      case ChainISA::MulAlpha:
        body += "  vec4 " + local + " = " + in0 + " * " + in1 + ".a;\n";
        break;
      case ChainISA::AARectCoverage:
        body += "  AARectCoverageData " + data + " = chainLoadAARectCoverage(" + dataAt + ");\n";
        if (pureOps) {
          body += "  vec4 " + local + " = tgfxOpAARectCoverage(" + in0 + ", " + data +
                  ".rect, gl_FragCoord.xy);\n";
        } else {
          body += "  vec4 " + local + " = chainAARectCoverage(" + in0 + ", " + data + ".rect);\n";
        }
        break;
      case ChainISA::LocalRectCoverage:
        body +=
            "  LocalRectCoverageData " + data + " = chainLoadLocalRectCoverage(" + dataAt + ");\n";
        if (pureOps) {
          body += "  vec4 " + local + " = tgfxOpLocalRectCoverage(" + in0 + ", " + data +
                  ".rect, " + data + ".deviceToLocal, gl_FragCoord.xy);\n";
        } else {
          body += "  vec4 " + local + " = " + in0 + " * vec4(analyticLocalRectCoverage(" + data +
                  ".rect, " + data + ".deviceToLocal));\n";
        }
        break;
      case ChainISA::RRectCoverage:
        body += "  RRectCoverageData " + data + " = chainLoadRRectCoverage(" + dataAt + ");\n";
        if (pureOps) {
          body += "  vec4 " + local + " = tgfxOpRRectCoverage(" + in0 + ", " + data + ".rect, " +
                  data + ".radiiX, " + data + ".radiiY, " + data + ".antiAlias, " + data +
                  ".deviceToLocal, gl_FragCoord.xy);\n";
        } else {
          body += "  vec4 " + local + " = aotApplyRRectCoverage(" + in0 + ", " + data + ".rect, " +
                  data + ".radiiX, " + data + ".radiiY, " + data + ".antiAlias, " + data +
                  ".deviceToLocal);\n";
        }
        break;
      case ChainISA::Gradient:
        // The gradient's parameters sit in the chain-wide block region, which the shared
        // evaluator addresses itself; the literal op word satisfies its Packed.x check.
        body += "  vec4 " + local + " = vec4(0.0);\n";
        body += "  evalGradientSlot(ivec4(" + std::to_string(ChainISA::Gradient) + ", 0, 0, 0), " +
                in0 + ", " + local + ");\n";
        break;
      case ChainISA::PerlinNoise:
        body += "  vec4 " + local + " = vec4(0.0);\n";
        body += "  evalNoiseSlot(ivec4(" + std::to_string(ChainISA::PerlinNoise) + ", 0, 0, 0), " +
                dataAt + ", " + local + ");\n";
        break;
      default:
        // An operator the emitter has not learned: decline so the caller keeps the interpreter.
        return {};
    }
    registers[slot.outRegister] = local;
    if (composition.rootRegister == slot.outRegister) {
      body += "  result = " + local + ";\n";
    }
    if (composition.clipCoverageRegister == slot.outRegister) {
      body += "  clipRegister = " + local + ";\n";
    }
    if (composition.coverageRootRegister == slot.outRegister) {
      body += "  coverageRootRegister = " + local + ";\n";
    }
  }
  if (composition.xpNeedsBlendPass) {
    // The XferProcessor's advanced blend pass: the loop's extra iteration, spelled directly. The
    // mode stays the XPBlendMode uniform exactly as the interpreter reads it.
    body += "  chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);\n";
    body += "  xpBlended = xpBlendColors(chainXPSrc, chainXPDst, XPBlendMode);\n";
  }
  return body;
}

bool ComposeChainFragmentGLSL(const ChainComposition& composition, std::string* code) {
  if (code == nullptr) {
    return false;
  }
  const auto begin = code->find(ExecutorBeginPragma);
  if (begin == std::string::npos) {
    return false;
  }
  const auto end = code->find(ExecutorEndPragma, begin);
  if (end == std::string::npos) {
    return false;
  }
  const auto endOfLine = code->find('\n', end);
  if (endOfLine == std::string::npos) {
    return false;
  }
  const std::string body = EmitComposedChainBodyGLSL(composition);
  if (body.empty()) {
    return false;
  }
  code->replace(begin, endOfLine - begin, body);
  return true;
}

std::string SerializeChainComposition(const ChainComposition& composition) {
  std::string out = "ccv1|";
  out += std::to_string(composition.slots.size()) + "|";
  for (const auto& slot : composition.slots) {
    out += std::to_string(slot.op) + "," + std::to_string(slot.in0) + "," +
           std::to_string(slot.in1) + "," + std::to_string(slot.outRegister) + "," +
           std::to_string(slot.selector) + "," + std::to_string(slot.blendMode) + "," +
           (slot.multiplyInputAlpha ? "1" : "0") + "," + std::to_string(slot.inputMode) + ";";
  }
  out += "|" + std::to_string(composition.rootRegister) + "," +
         std::to_string(composition.coverageRootRegister) + "," +
         std::to_string(composition.clipCoverageRegister) + "," +
         (composition.xpNeedsBlendPass ? "1" : "0");
  out += "|" + std::to_string(composition.usedOps) + "," + std::to_string(composition.blendModes) +
         "," + std::to_string(composition.textureFlags) + "," +
         std::to_string(composition.gradientKinds);
  return out;
}

uint64_t ChainCompositionKey(uint32_t variantId, const ChainComposition& composition) {
  const std::string serialized = SerializeChainComposition(composition);
  // FNV-1a over the variant id's four bytes then the serialization: every input byte changes the
  // key, and the hash is stable across machines because the serialization is plain ASCII.
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
      hash ^= bytes[index];
      hash *= 1099511628211ull;
    }
  };
  mix(&variantId, sizeof(variantId));
  mix(serialized.data(), serialized.size());
  return hash;
}

uint32_t ComposedFragmentId(uint32_t variantId, const ChainComposition& composition) {
  const uint64_t key = ChainCompositionKey(variantId, composition);
  return 0x80000000u | static_cast<uint32_t>(key & 0x7FFFFFFFu);
}

bool PrependChainSpecDefinesGLSL(std::string* code,
                                 const std::vector<std::pair<uint32_t, int32_t>>& idValues) {
  if (code == nullptr) {
    return false;
  }
  const auto versionPos = code->find("#version");
  if (versionPos == std::string::npos) {
    return false;
  }
  const auto versionEnd = code->find('\n', versionPos);
  if (versionEnd == std::string::npos) {
    return false;
  }
  std::string defines;
  for (const auto& idValue : idValues) {
    defines += "#define TGFX_SPEC_" + std::to_string(idValue.first) + " " +
               std::to_string(idValue.second) + "\n";
  }
  code->insert(versionEnd + 1, defines);
  return true;
}

std::map<std::string, std::vector<int32_t>> ResolveCompositionSpecValues(
    const ChainComposition& composition, size_t slotCapacity) {
  std::map<std::string, std::vector<int32_t>> values = {};
  values["SlotCount"] = {static_cast<int32_t>(composition.slots.size())};
  std::vector<int32_t> packed = {};
  for (size_t index = 0; index < slotCapacity; ++index) {
    if (index < composition.slots.size()) {
      const auto& slot = composition.slots[index];
      packed.push_back(slot.op);
      packed.push_back(slot.in0);
      packed.push_back(slot.in1);
      packed.push_back(slot.selector);
    } else {
      // The inactive tail's record is the zeroed slot, the same value the runtime's resolution
      // uploads: OP_NONE with no inputs and no selector.
      packed.push_back(ChainISA::None);
      packed.push_back(-2);
      packed.push_back(-2);
      packed.push_back(0);
    }
  }
  values["SlotPacked"] = std::move(packed);
  values["CoverageRootIndex"] = {static_cast<int32_t>(composition.coverageRootRegister)};
  values["RootIndex"] = {static_cast<int32_t>(composition.rootRegister)};
  values["ClipCoverageRegister"] = {static_cast<int32_t>(composition.clipCoverageRegister)};
  values["UsedOps"] = {static_cast<int32_t>(composition.usedOps)};
  values["BlendModes"] = {static_cast<int32_t>(composition.blendModes)};
  values["TextureFlags"] = {static_cast<int32_t>(composition.textureFlags)};
  values["GradientKinds"] = {static_cast<int32_t>(composition.gradientKinds)};
  bool needsBlendMath = composition.xpNeedsBlendPass;
  for (const auto& slot : composition.slots) {
    if (slot.op == ChainISA::Blend) {
      needsBlendMath = true;
    }
  }
  values["NeedsBlendMath"] = {needsBlendMath ? 1 : 0};
  return values;
}

}  // namespace tgfx
