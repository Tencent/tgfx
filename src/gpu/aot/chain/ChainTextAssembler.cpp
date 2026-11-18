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
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {
namespace {

// The assembled slice's operator vocabulary: the pure pointwise color operators, Blend, the
// inline expressions, the analytic coverage operators and plain texture leaves. Everything else
// (tiled recipes, gradients, noise, the macro-based color-space xform) keeps the interpreter's
// declarations until its entry material is packed too.
bool OpAssembles(int op) {
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
    case ChainISA::Texture:
    case ChainISA::TexModulate:
      return true;
    default:
      return false;
  }
}

// The entry's text copied out; an empty result means the entry is absent.
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

// Leaf-position uniforms carry a _<leaf> suffix past the first leaf, matching the declaration
// chunk's std140 members.
std::string LeafName(const char* base, int leaf) {
  return leaf == 0 ? std::string(base) : std::string(base) + "_" + std::to_string(leaf);
}

}  // namespace

bool ChainCompositionAssembles(const ChainComposition& composition) {
  if (composition.xpNeedsBlendPass) {
    return false;
  }
  // Shader-tiled leaves need the tiling module's entry material, which the packed slice does not
  // carry yet; they keep the interpreter's declarations.
  if ((composition.textureFlags & 2) != 0) {
    return false;
  }
  for (const auto& slot : composition.slots) {
    if (!OpAssembles(slot.op)) {
      return false;
    }
  }
  return true;
}

bool AssembleChainFragmentGLSL(const ChainComposition& composition, uint32_t variantId,
                               const PrecompiledShaderCache* cache, std::string* code) {
  if (code == nullptr || !ChainCompositionAssembles(composition)) {
    return false;
  }
  const std::string chunk = PackText(cache, FragmentPackChunkId(variantId));
  const std::string loaders = PackText(cache, FragmentPackLoaders);
  const std::string tail = PackText(cache, FragmentPackTail);
  if (chunk.empty() || loaders.empty() || tail.empty()) {
    return false;
  }
  // The used fragments in first-use order and the dependency texts their files call into: the
  // dependencies come first so the fragment bodies resolve.
  std::set<int> usedOps = {};
  bool needsAnalyticClipMath = false;
  bool needsTextureSwizzle = false;
  bool needsLeafFetch = false;
  for (const auto& slot : composition.slots) {
    if (slot.op == ChainISA::None) {
      continue;
    }
    usedOps.insert(slot.op);
    if (slot.op == ChainISA::LocalRectCoverage || slot.op == ChainISA::RRectCoverage) {
      needsAnalyticClipMath = true;
    }
    if (slot.op == ChainISA::Texture || slot.op == ChainISA::TexModulate) {
      needsTextureSwizzle = true;
      needsLeafFetch = true;
    }
  }
  std::string fragments;
  auto appendDep = [&](uint32_t id) {
    const std::string text = PackText(cache, id);
    if (text.empty()) {
      return false;
    }
    fragments += text + "\n";
    return true;
  };
  if (needsTextureSwizzle && !appendDep(FragmentPackDepTextureSwizzle)) {
    return false;
  }
  if (needsLeafFetch && !appendDep(FragmentPackDepLeafFetch)) {
    return false;
  }
  if (needsAnalyticClipMath && !appendDep(FragmentPackDepAnalyticClipMath)) {
    return false;
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
  // The leaf prefetches: plain fetches at the leaf positions, the same calls the interpreter's
  // main emits for a non-tiled draw.
  std::string prefetch;
  for (size_t index = 0; index < composition.slots.size() && index < 4; ++index) {
    if (composition.slots[index].op != ChainISA::Texture) {
      continue;
    }
    const std::string leaf = std::to_string(index);
    prefetch += "  chainLeafTex[" + leaf + "] = chainPlainLeafFetch(TextureSampler_" + leaf +
                ", TransformedCoords_" + leaf + ", " + LeafName("Subset", static_cast<int>(index)) +
                ", " + LeafName("AlphaStart", static_cast<int>(index)) + ", " +
                LeafName("HasRgbaaa", static_cast<int>(index)) + ", " +
                LeafName("SubsetClampOn", static_cast<int>(index)) + ", " + leaf + ");\n";
  }
  // The straight-line body: the pure-mode statements, with the registers declared by the wrapper.
  const std::string body = EmitComposedChainBodyGLSL(composition, /*pureOps=*/true);
  if (body.empty()) {
    return false;
  }
  // The tail's two placeholders are structural facts of this composition.
  const std::string clip = composition.clipCoverageRegister >= 0 ? "clipRegister" : "vec4(1.0)";
  const std::string coverage =
      composition.coverageRootRegister >= 0 ? "coverageRootRegister" : "chainUnitCoverageSource()";
  std::string tailText = tail;
  if (!ReplaceAll(&tailText, "__TGFX_CLIP_COVERAGE__", clip) ||
      !ReplaceAll(&tailText, "__TGFX_FINAL_COVERAGE__", coverage)) {
    return false;
  }
  // The chunk is the variant's own text prefix, so it begins with #version already.
  *code = chunk + "\n" + loaders + "\n" + fragments +
          "\nvoid main() {\n"
          "  vec4 result = vec4(0.0);\n"
          "  vec4 clipRegister = vec4(0.0);\n"
          "  vec4 coverageRootRegister = vec4(0.0);\n"
          "  vec4 chainLeafTex[4];\n" +
          prefetch + body + tailText + "}\n";
  // The dump aid: with TGFX_ASSEMBLED_DUMP pointing at a directory, every assembled text lands
  // there as assembled_N.glsl for inspection. Read-only diagnostic, no effect on the pipeline.
  if (const char* dumpDir = std::getenv("TGFX_ASSEMBLED_DUMP")) {
    static std::atomic<int> dumpCount = {0};
    const int index = dumpCount.fetch_add(1);
    std::ofstream file(std::string(dumpDir) + "/assembled_" + std::to_string(index) + ".glsl",
                       std::ios::binary);
    file << *code;
  }
  return true;
}

}  // namespace tgfx
