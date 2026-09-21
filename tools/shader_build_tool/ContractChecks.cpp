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

#include "ContractChecks.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

static bool HasUniform(const StageReflectionData& reflection, const std::string& name) {
  return std::any_of(reflection.uniforms.begin(), reflection.uniforms.end(),
                     [&name](const UniformEntry& entry) { return entry.name == name; });
}

// Every shader whose fragment block carries the clip contract marker (HasClip) must declare the
// whole contract: a missing field means the onSetData writers (which reference the same
// ClipContract constants) would silently skip their upload and the clip would read stale data.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants) {
  const std::string contractFields[] = {
      ClipContract::Rect,          ClipContract::HasClip, ClipContract::LocalRect,
      ClipContract::RadiiX,        ClipContract::RadiiY,  ClipContract::AntiAlias,
      ClipContract::DeviceToLocal,
  };
  size_t errors = 0;
  for (const auto& variant : variants) {
    if (!HasUniform(variant.fragmentReflection, ClipContract::HasClip)) {
      continue;
    }
    for (const auto& field : contractFields) {
      if (!HasUniform(variant.fragmentReflection, field)) {
        std::cerr << "[ContractCheck] " << variant.shaderName << " (frag "
                  << variant.fragPermutationIndex
                  << ") carries the clip contract but is missing uniform '" << field << "'\n";
        ++errors;
      }
    }
  }
  return errors;
}

// Parses every "#define OP_<NAME> <decimal>" line of an include into a name->value map. The value
// must be a bare decimal integer: stoi alone accepts prefixes like "0 + 1", which would silently
// pass a malformed define. Unparseable or non-standalone values are reported as errors.
static std::map<std::string, int> ParseOpDefines(const std::string& source, const char* fileName,
                                                 size_t* errors) {
  std::map<std::string, int> defines;
  size_t scanPosition = 0;
  while (true) {
    auto defineStart = source.find("#define OP_", scanPosition);
    if (defineStart == std::string::npos) {
      break;
    }
    auto nameStart = defineStart + 8;  // past "#define "
    auto nameEnd = source.find(' ', nameStart);
    if (nameEnd == std::string::npos) {
      break;
    }
    auto defineName = source.substr(nameStart, nameEnd - nameStart);
    auto valueStart = source.find_first_not_of(" \t", nameEnd);
    auto valueEnd = source.find('\n', valueStart);
    auto valueText = source.substr(valueStart, valueEnd - valueStart);
    while (!valueText.empty() &&
           (valueText.back() == ' ' || valueText.back() == '\t' || valueText.back() == '\r')) {
      valueText.pop_back();
    }
    bool allDigits = !valueText.empty();
    for (char ch : valueText) {
      if (ch < '0' || ch > '9') {
        allDigits = false;
        break;
      }
    }
    if (!allDigits) {
      std::cerr << "[ContractCheck] cannot parse " << defineName << " value '" << valueText
                << "' in " << fileName << " (expected a bare decimal integer)\n";
      ++*errors;
    } else {
      defines[defineName] = std::stoi(valueText);
    }
    scanPosition = nameEnd;
  }
  return defines;
}

// Validates one OP_* include against an expected name->value set: every expected define must
// exist with the exact value, and no other OP_* define may appear (a stray define means the
// kernel grew an opcode the C++ side has not anchored yet, which would surface as
// misinterpretation at runtime since the evaluation switches have no default branch).
static size_t ValidateOpDefines(const std::string& shaderDir, const char* fileName,
                                const std::map<std::string, int>& expected) {
  std::ifstream file(shaderDir + "/level1/" + fileName);
  if (!file.is_open()) {
    std::cerr << "[ContractCheck] cannot open " << fileName << " under " << shaderDir << "\n";
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  size_t errors = 0;
  auto defines = ParseOpDefines(buffer.str(), fileName, &errors);
  for (const auto& [name, value] : expected) {
    auto it = defines.find(name);
    if (it == defines.end()) {
      std::cerr << "[ContractCheck] OP define missing in " << fileName << ": " << name << "\n";
      ++errors;
    } else if (it->second != value) {
      std::cerr << "[ContractCheck] " << name << " is " << it->second << " but the C++ contract "
                << "says " << value << "\n";
      ++errors;
    }
  }
  for (const auto& [name, value] : defines) {
    if (expected.find(name) == expected.end()) {
      std::cerr << "[ContractCheck] unknown OP define in " << fileName << ": " << name
                << " (anchor it in the C++ contract or remove the define)\n";
      ++errors;
    }
  }
  return errors;
}

// The chain slot op codes ride the Packed.x selector, so the GLSL OP_* defines and the C++
// AOTChainOp enum (anchored to the ChainOp constants) must agree value-for-value.
size_t ValidateChainOpCodes(const std::string& shaderDir) {
  return ValidateOpDefines(shaderDir, "pointwise_chain_eval.inc",
                           {{"OP_COLOR_MATRIX", ChainOp::ColorMatrix},
                            {"OP_LUMA", ChainOp::Luma},
                            {"OP_ALPHA_THRESHOLD", ChainOp::AlphaThreshold},
                            {"OP_COLOR_SPACE_XFORM", ChainOp::ColorSpaceXform},
                            {"OP_NONE", ChainOp::None},
                            {"OP_TEXTURE", ChainOp::Texture},
                            {"OP_CONST_COLOR", ChainOp::ConstColor},
                            {"OP_BLEND", ChainOp::Blend},
                            {"OP_AARECT_COVERAGE", ChainOp::AARectCoverage},
                            {"OP_GRADIENT", ChainOp::Gradient},
                            {"OP_LOCAL_RECT_COVERAGE", ChainOp::LocalRectCoverage},
                            {"OP_RRECT_COVERAGE", ChainOp::RRectCoverage},
                            {"OP_TEX_MODULATE", ChainOp::TexModulate},
                            {"OP_INPUT_OPAQUE", ChainOp::InputOpaque},
                            {"OP_MUL_ALPHA", ChainOp::MulAlpha}});
}

// The plain pointwise slot selects its operator the same way, so pointwise_op.inc must agree
// with the AOTPointwiseOpType enum (anchored to the PointwiseOp constants). Chain-only operators
// (Texture and the coverage/modulator ops) must not appear here.
size_t ValidatePointwiseOpCodes(const std::string& shaderDir) {
  return ValidateOpDefines(shaderDir, "pointwise_op.inc",
                           {{"OP_COLOR_MATRIX", PointwiseOp::ColorMatrix},
                            {"OP_LUMA", PointwiseOp::Luma},
                            {"OP_ALPHA_THRESHOLD", PointwiseOp::AlphaThreshold},
                            {"OP_COLOR_SPACE_XFORM", PointwiseOp::ColorSpaceXform},
                            {"OP_NONE", PointwiseOp::None},
                            {"OP_CONST_COLOR", PointwiseOp::ConstColor},
                            {"OP_BLEND", PointwiseOp::Blend}});
}

// Reflection-level invariants the runtime loader enforces on read: catching them at build time
// keeps a broken writer from shipping bundles that the loader would reject (or worse, accept with
// gaps in the sampler binding table).
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants) {
  size_t errors = 0;
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      std::map<std::string, int> seenNames;
      for (const auto* list : {&reflection->uniforms, &reflection->samplers}) {
        bool isSamplerList = list == &reflection->samplers;
        for (const auto& entry : *list) {
          if (entry.name.find('\0') != std::string::npos) {
            std::cerr << "[ContractCheck] " << variant.shaderName
                      << " reflection carries an embedded NUL in a name\n";
            ++errors;
            continue;
          }
          if (++seenNames[entry.name] > 1) {
            std::cerr << "[ContractCheck] " << variant.shaderName << " declares duplicate name '"
                      << entry.name << "' in one stage\n";
            ++errors;
          }
          if (entry.arraySize < 1 || (isSamplerList && entry.arraySize != 1)) {
            std::cerr << "[ContractCheck] " << variant.shaderName << " entry '" << entry.name
                      << "' has arraySize " << entry.arraySize << " (uniforms need >= 1, samplers "
                      << "need exactly 1)\n";
            ++errors;
          }
        }
      }
      // Ordinary texture bindings must use the generic TextureSampler_<N> pattern with N dense
      // from zero (a gap would shift every later binding on the runtime side); dedicated-purpose
      // samplers use their registered semantic names instead (SamplerContract in KernelContract.h).
      int nextSamplerIndex = 0;
      for (const auto& sampler : reflection->samplers) {
        const std::string prefix = "TextureSampler_";
        bool generic = sampler.name.size() > prefix.size() &&
                       sampler.name.compare(0, prefix.size(), prefix) == 0;
        if (generic) {
          auto digits = sampler.name.substr(prefix.size());
          generic = !digits.empty() && std::all_of(digits.begin(), digits.end(),
                                                   [](char ch) { return ch >= '0' && ch <= '9'; });
        }
        if (SamplerContract::IsSemanticSamplerName(sampler.name)) {
          continue;
        }
        int parsed = -1;
        if (generic) {
          parsed = std::stoi(sampler.name.substr(prefix.size()));
        }
        if (!generic) {
          std::cerr << "[ContractCheck] " << variant.shaderName << " declares sampler '"
                    << sampler.name << "' (neither the TextureSampler_<N> pattern nor a "
                    << "registered semantic name; see SamplerContract in KernelContract.h)\n";
          ++errors;
        } else if (parsed != nextSamplerIndex) {
          std::cerr << "[ContractCheck] " << variant.shaderName << " generic sampler #"
                    << nextSamplerIndex << " is named '" << sampler.name
                    << "' (indices must be dense from zero)\n";
          ++errors;
        }
        ++nextSamplerIndex;
      }
    }
  }
  return errors;
}

}  // namespace tgfx
