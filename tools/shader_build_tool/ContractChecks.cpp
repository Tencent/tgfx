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

// The chain slot op codes ride the Packed.x selector, so the GLSL OP_* defines and the C++
// AOTChainOp enum (anchored to the ChainOp constants) must agree value-for-value. The GLSL side
// is plain text, so this parses the defines and compares against the constants.
size_t ValidateChainOpCodes(const std::string& shaderDir) {
  std::ifstream file(shaderDir + "/level1/pointwise_chain_eval.inc");
  if (!file.is_open()) {
    std::cerr << "[ContractCheck] cannot open pointwise_chain_eval.inc under " << shaderDir << "\n";
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string source = buffer.str();

  struct ExpectedOp {
    const char* name;
    int value;
  };
  const ExpectedOp expected[] = {
      {"OP_COLOR_MATRIX", ChainOp::ColorMatrix},
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
      {"OP_MUL_ALPHA", ChainOp::MulAlpha},
  };

  size_t errors = 0;
  for (const auto& op : expected) {
    // The defines carry no padding around the value ("#define OP_X <n>"), and the extracted
    // number must stand alone on the line.
    std::string pattern = std::string("#define ") + op.name + " ";
    auto position = source.find(pattern);
    if (position == std::string::npos) {
      std::cerr << "[ContractCheck] OP define missing in pointwise_chain_eval.inc: " << op.name
                << "\n";
      ++errors;
      continue;
    }
    auto valueStart = position + pattern.size();
    auto valueEnd = source.find('\n', valueStart);
    auto valueText = source.substr(valueStart, valueEnd - valueStart);
    // Trim surrounding whitespace.
    while (!valueText.empty() &&
           (valueText.front() == ' ' || valueText.front() == '\t' || valueText.front() == '\r')) {
      valueText.erase(valueText.begin());
    }
    while (!valueText.empty() &&
           (valueText.back() == ' ' || valueText.back() == '\t' || valueText.back() == '\r')) {
      valueText.pop_back();
    }
    // The whole trimmed text must be a decimal integer: stoi alone accepts prefixes like
    // "0 + 1" (parsing 0 and ignoring the rest), which would silently pass a malformed define.
    bool allDigits = !valueText.empty();
    for (char ch : valueText) {
      if (ch < '0' || ch > '9') {
        allDigits = false;
        break;
      }
    }
    if (!allDigits) {
      std::cerr << "[ContractCheck] cannot parse " << op.name << " value '" << valueText
                << "' (expected a bare decimal integer)\n";
      ++errors;
      continue;
    }
    int parsed = std::stoi(valueText);
    if (parsed != op.value) {
      std::cerr << "[ContractCheck] " << op.name << " is " << parsed << " but AOTChainOp says "
                << op.value << "\n";
      ++errors;
    }
  }
  // An OP_* define outside the expected set means the kernel grew a new opcode the C++ side has
  // not anchored yet — fail loudly instead of letting the mismatch surface as misinterpretation
  // at runtime (the kernel's evaluation switch has no default branch).
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
    bool known = false;
    for (const auto& op : expected) {
      if (defineName == op.name) {
        known = true;
        break;
      }
    }
    if (!known) {
      std::cerr << "[ContractCheck] unknown OP define in pointwise_chain_eval.inc: " << defineName
                << " (add it to AOTChainOp or remove the define)\n";
      ++errors;
    }
    scanPosition = nameEnd;
  }
  return errors;
}

}  // namespace tgfx
