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

#include "gpu/aot/chain/ChainTextAssembler.h"
#include <set>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {
namespace {

// The rect-entry slice's operator vocabulary: the pure pointwise color operators, Blend, the
// inline expressions and the analytic coverage operators. Everything else (texture leaves,
// gradients, noise, the macro-based color-space xform) keeps the interpreter's declarations.
bool OpAssemblesRect(int op) {
  switch (op) {
    case ChainISA::None:
    case ChainISA::ConstColor:
    case ChainISA::ColorMatrix:
    case ChainISA::Luma:
    case ChainISA::AlphaThreshold:
    case ChainISA::Blend:
    case ChainISA::InputOpaque:
    case ChainISA::MulAlpha:
    case ChainISA::AARectCoverage:
    case ChainISA::LocalRectCoverage:
    case ChainISA::RRectCoverage:
      return true;
    default:
      return false;
  }
}

// The entry's text copied out (the blob's data is compressed-pool owned and this helper keeps the
// lookup and the copy together); an empty result means the entry is absent.
std::string PackText(const PrecompiledShaderCache* cache, uint32_t id) {
  const auto* blob = cache->findPackEntry(id);
  if (blob == nullptr) {
    return {};
  }
  return std::string(blob->data.begin(), blob->data.end());
}

bool ReplaceAll(std::string* text, const std::string& token, const std::string& value) {
  const auto position = text->find(token);
  if (position == std::string::npos) {
    return false;
  }
  text->replace(position, token.size(), value);
  return true;
}

}  // namespace

bool ChainCompositionAssemblesRect(const ChainComposition& composition) {
  if (composition.xpNeedsBlendPass) {
    return false;
  }
  for (const auto& slot : composition.slots) {
    if (!OpAssemblesRect(slot.op)) {
      return false;
    }
  }
  return true;
}

bool AssembleChainFragmentRectGLSL(const ChainComposition& composition,
                                   const PrecompiledShaderCache* cache, std::string* code) {
  if (code == nullptr || !ChainCompositionAssemblesRect(composition)) {
    return false;
  }
  const std::string entry = PackText(cache, FragmentPackEntryRect);
  const std::string loaders = PackText(cache, FragmentPackLoaders);
  const std::string tail = PackText(cache, FragmentPackTail);
  if (entry.empty() || loaders.empty() || tail.empty()) {
    return false;
  }
  // The used fragments in first-use order and the dependencies their files call into: the
  // dependency texts come first so the fragment bodies resolve.
  std::set<int> usedOps = {};
  bool needsAnalyticClipMath = false;
  for (const auto& slot : composition.slots) {
    if (slot.op == ChainISA::None) {
      continue;
    }
    usedOps.insert(slot.op);
    if (slot.op == ChainISA::LocalRectCoverage || slot.op == ChainISA::RRectCoverage) {
      needsAnalyticClipMath = true;
    }
  }
  std::string fragments;
  if (needsAnalyticClipMath) {
    const std::string analytic = PackText(cache, FragmentPackDepAnalyticClipMath);
    if (analytic.empty()) {
      return false;
    }
    fragments += analytic + "\n";
  }
  for (int op : usedOps) {
    const auto* def = FindFragment(op);
    if (def == nullptr || def->sourceFile == nullptr) {
      continue;
    }
    const std::string text = PackText(cache, 0x40000000u | static_cast<uint32_t>(op));
    if (text.empty()) {
      return false;
    }
    fragments += text + "\n";
  }
  // The straight-line body: the same statements the splice path emits, with the three registers
  // declared by the wrapper main below.
  const std::string body = EmitComposedChainBodyGLSL(composition);
  if (body.empty()) {
    return false;
  }
  // The tail's two placeholders are structural facts of this composition: the clip coverage and
  // the final coverage before the clip folds in (the geometry's own coverage path — a coverage
  // root replaces the varying, matching the interpreter's epilogue contract).
  const std::string clip = composition.clipCoverageRegister >= 0 ? "clipRegister" : "vec4(1.0)";
  const std::string coverage =
      composition.coverageRootRegister >= 0 ? "coverageRootRegister" : "chainUnitCoverageSource()";
  std::string tailText = tail;
  if (!ReplaceAll(&tailText, "__TGFX_CLIP_COVERAGE__", clip) ||
      !ReplaceAll(&tailText, "__TGFX_FINAL_COVERAGE__", coverage)) {
    return false;
  }
  *code = "#version 330\n" + entry + "\n" + loaders + "\n" + fragments +
          "\nvoid main() {\n"
          "  vec4 result = vec4(0.0);\n"
          "  vec4 clipRegister = vec4(0.0);\n"
          "  vec4 coverageRootRegister = vec4(0.0);\n" +
          body + tailText + "}\n";
  return true;
}

}  // namespace tgfx
