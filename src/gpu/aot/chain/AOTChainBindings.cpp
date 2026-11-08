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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/chain/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

// The binding record of one TextureSource leaf: a plain leaf sampling through its resolved sampler
// state, the stand-in leaf of a device-space source, or a tiled leaf whose wrap modes the kernel's
// single-tap path implements. Only a plain leaf may carry the RGBAAA layout, whose alpha-area
// offset rides the kernel's per-leaf AlphaStart / HasRgbaaa fields. The record carries exactly the
// data the leaf's TextureEffect is constructed from (see MaterializeChainChildren).
static bool BuildTextureLeafBinding(const AOTEffectNode* node, AOTBinding* binding) {
  auto parameters = node != nullptr && node->kind == AOTEffectKind::TextureSource
                        ? std::get_if<AOTTextureParameters>(&node->parameters)
                        : nullptr;
  if (parameters == nullptr || parameters->textureProxy == nullptr ||
      (parameters->hasRGBAAA && parameters->samplingKind != AOTTextureSamplingKind::Plain)) {
    return false;
  }
  binding->kind = AOTBinding::Kind::Leaf;
  binding->textureProxy = parameters->textureProxy;
  if (parameters->samplingKind == AOTTextureSamplingKind::Device) {
    // The chain kernel samples this leaf at its DeviceLeafCoordMatrix * gl_FragCoord (see
    // AOTChainDeviceLeaf), so the child only binds the texture with the sampler state of a
    // DeviceSpaceTextureEffect (clamp, linear, no mipmaps). Its coordinate transform is unused.
    // The runtime effect's emission is a bare texture lookup — no subset clamp for any view fact
    // (a smaller-than-proxy view, an External texture) — so this leaf's SubsetClampOn gate must be
    // 0 unconditionally, not follow needSubset(). The unbounded sample area is what forces it;
    // without it, a view-mismatched device leaf would gain a clamp the runtime never emits.
    SamplingArgs args = {TileMode::Clamp, TileMode::Clamp,
                         SamplingOptions(FilterMode::Linear, MipmapMode::None),
                         SrcRectConstraint::Fast};
    args.sampleArea = Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f);
    binding->args = args;
    binding->uvMatrix = Matrix::I();
    return true;
  }
  if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
    const auto& recipe = parameters->tiledRecipe;
    if (!recipe.has_value() || !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) ||
        !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY)) {
      return false;
    }
    // The hardware sampler performs all tiling (wrap modes and clamp-to-border), so the leaf is a
    // plain TextureEffect carrying the resolved sampler state. A GLSL-side subset clamp would
    // blunt the wrap at the subset edge, so this leaf's gate stays 0 whatever the view facts say;
    // the unbounded sample area forces it (the same statement the device leaf makes above).
    SamplingOptions sampling(recipe->hardwareSampler.minFilterMode,
                             recipe->hardwareSampler.magFilterMode,
                             recipe->hardwareSampler.mipmapMode);
    SamplingArgs args = {recipe->hardwareSampler.tileModeX, recipe->hardwareSampler.tileModeY,
                         sampling, SrcRectConstraint::Fast};
    args.sampleArea = Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f);
    binding->args = args;
    binding->uvMatrix = parameters->uvMatrix;
    return true;
  }
  SamplingOptions sampling(parameters->samplerState.minFilterMode,
                           parameters->samplerState.magFilterMode,
                           parameters->samplerState.mipmapMode);
  SamplingArgs args = {parameters->samplerState.tileModeX, parameters->samplerState.tileModeY,
                       sampling, parameters->constraint};
  args.sampleArea = parameters->subset;
  binding->args = args;
  binding->uvMatrix = parameters->uvMatrix;
  binding->alphaStart = parameters->alphaStart;
  return true;
}

// Flattens a single-pass pointwise DAG into the fused chain layout. Texture leaves are placed in
// the leading slots so that slot k pairs with TextureSampler_k, which keeps sampler indexing
// static in the kernel. When clipSlots carries analytic clip FPs, their coverage slots form the
// clip-coverage channel. When coverageGraph is given, its nodes (lowered from the coverage FP with
// the GeometryCoverage unit as origin) are merged after the color nodes: coverage leaves extend
// the leading texture block, coverage ops trail the color ops, and coverageRoot becomes the
// chain's coverage root.
bool PlanChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                     const AOTEffectGraph* coverageGraph, AOTNodeID coverageRoot,
                     bool coverageLeafFromUVCoord, const std::vector<AOTNodeID>& clipLinks,
                     AOTChainLayout* layout) {
  AOTChainContext ctx = {};
  ctx.graph = &graph;
  ctx.pass = &pass;
  ctx.coverageGraph = coverageGraph;
  ctx.coverageRoot = coverageRoot;
  ctx.coverageLeafFromUVCoord = coverageLeafFromUVCoord;
  ctx.clipLinks = &clipLinks;
  ctx.layout = layout;
  if (!MergeNodeSpaces(&ctx)) {
    return false;
  }
  ScheduleChainSlots(&ctx);
  if (!EncodeChainSlots(&ctx) || !ResolveFinalChainProfile(&ctx)) {
    return false;
  }
  if (!ResolveCoverageRoot(&ctx)) {
    return false;
  }
  RouteAnalyticChains(&ctx);
  return AssignChainRegisters(&ctx) && FinalizeChainLayout(&ctx);
}

// The padding state that mirrors the first real leaf's sampler on the phantom children: on
// OpenGL the wrap/filter modes are texture-object state (not per-unit), so a default-state
// phantom bound after a real leaf would overwrite e.g. a decal clamp-to-border with
// clamp-to-edge for the whole draw.
struct ChainPaddingTexture {
  std::shared_ptr<TextureProxy> proxy = nullptr;
  SamplerState state = {};
};

static SamplingArgs PaddingSamplingArgs(const SamplerState& state) {
  SamplingOptions sampling(state.minFilterMode, state.magFilterMode, state.mipmapMode);
  return {state.tileModeX, state.tileModeY, sampling, SrcRectConstraint::Fast};
}

// One candidate for the padding texture: a resolved texture leaf (its tiled hardware sampler when
// it has one), or a LUT-gradient-only chain's baked LUT texture — a chain with no TextureSource
// node (no leaf binding exists there, so the LUT serves as the padding just as it served as the
// sampler child).
static void ConsiderPaddingNode(const AOTEffectNode* node, ChainPaddingTexture* padding) {
  if (padding->proxy != nullptr || node == nullptr) {
    return;
  }
  if (const auto* texture = std::get_if<AOTTextureParameters>(&node->parameters)) {
    padding->proxy = texture->textureProxy;
    if (texture->samplingKind == AOTTextureSamplingKind::Tiled &&
        texture->tiledRecipe.has_value()) {
      padding->state = texture->tiledRecipe->hardwareSampler;
    } else {
      padding->state = texture->samplerState;
    }
  } else if (const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters)) {
    if (gradient->lutProxy != nullptr) {
      padding->proxy = gradient->lutProxy;
    }
  }
}

// Picks the padding texture by walking the color graph (only its in-pass nodes when the chain
// carries noise — the noise graph's whole node space is the family's, so out-of-pass color nodes
// must not contribute) and the coverage graph. Falls back to the noise tables' first proxy with
// nearest sampling.
static ChainPaddingTexture ChoosePaddingTexture(const AOTEffectGraph& graph, const PassIR& pass,
                                                const AOTEffectGraph* coverageGraph,
                                                const AOTPerlinNoiseParameters* noise,
                                                std::shared_ptr<TextureProxy> noisePaddingProxy) {
  ChainPaddingTexture padding = {};
  for (size_t index = 0; index < graph.nodeCount(); ++index) {
    AOTNodeID nodeID(static_cast<uint32_t>(index));
    if (noise != nullptr &&
        std::find(pass.nodes.begin(), pass.nodes.end(), nodeID) == pass.nodes.end()) {
      continue;
    }
    ConsiderPaddingNode(graph.nodeAt(nodeID), &padding);
  }
  for (size_t index = 0; coverageGraph != nullptr && index < coverageGraph->nodeCount(); ++index) {
    ConsiderPaddingNode(coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index))), &padding);
  }
  if (padding.proxy == nullptr && noisePaddingProxy != nullptr) {
    padding.proxy = std::move(noisePaddingProxy);
    padding.state = SamplerState(SamplingOptions(FilterMode::Nearest));
  }
  return padding;
}

// Records the noise tables' two lookup textures as sampler-only bindings ahead of the phantom
// padding. They are real bindings the kernel reads through Nearest lookups; firstProxy reports
// the first one for the padding fallback. Returns false when a view cannot be wrapped.
static bool AppendNoiseLookupBindings(const AOTPerlinNoiseParameters* noise,
                                      std::shared_ptr<TextureProxy>* firstProxy,
                                      std::vector<AOTBinding>* bindings) {
  for (const auto& view : {noise->permutationsView, noise->noiseView}) {
    if (view == nullptr || view->getContext() == nullptr) {
      return false;
    }
    auto proxy = view->getContext()->proxyProvider()->wrapTextureView(view);
    if (proxy == nullptr) {
      return false;
    }
    SamplingArgs lookupArgs = {TileMode::Clamp, TileMode::Clamp,
                               SamplingOptions(FilterMode::Nearest), SrcRectConstraint::Fast};
    if (firstProxy != nullptr && *firstProxy == nullptr) {
      *firstProxy = proxy;
    }
    bindings->push_back({AOTBinding::Kind::Padding, proxy, lookupArgs, Matrix::I()});
  }
  return true;
}

// Fills the sampler slots up to the four-leaf artifacts' TEXTURE_COUNT with phantom bindings
// re-using the padding texture. A chain that already binds four (or none) is left alone.
static bool AppendPaddingBindings(const ChainPaddingTexture& padding, size_t* samplerChildren,
                                  std::vector<AOTBinding>* bindings) {
  if (*samplerChildren == 0 || *samplerChildren >= 4) {
    return true;
  }
  if (padding.proxy == nullptr) {
    return false;
  }
  auto paddingArgs = PaddingSamplingArgs(padding.state);
  while (*samplerChildren < 4) {
    bindings->push_back({AOTBinding::Kind::Padding, padding.proxy, paddingArgs, Matrix::I()});
    ++*samplerChildren;
  }
  return true;
}

// Records the mask slot binding last: the real device-space mask, or — for a mask-free chain that
// bound all four sampler slots — the phantom the kernel identifies by HasMaskTexture and skips.
// The phantom is recorded as padding (it is a plain texture child); it is the binding that sits
// one past samplerChildren, which is how the shell tells it apart.
static bool AppendMaskBinding(const AOTTextureParameters* mask, const ChainPaddingTexture& padding,
                              size_t samplerChildren, std::vector<AOTBinding>* bindings) {
  if (mask != nullptr) {
    // The mask child's Subset write routes to the kernel's DeviceMaskSubset decoy, so a leaf's
    // real subset rect survives in the shared uniform block.
    bindings->push_back({AOTBinding::Kind::Mask, mask->textureProxy, {}, mask->uvMatrix});
    return true;
  }
  if (samplerChildren != 4) {
    return true;
  }
  if (padding.proxy == nullptr) {
    return false;
  }
  bindings->push_back(
      {AOTBinding::Kind::Padding, padding.proxy, PaddingSamplingArgs(padding.state), Matrix::I()});
  return true;
}

// Merges the draw's coverage terms into ONE coverage graph for the layout pipeline: one
// geometry-coverage unit, the analytic terms' links chained onto it (each term's innermost link
// continues the previous term's outermost — a pure product), and — when a subtree or glyph term
// exists — its nodes copied over with the term's own unit replaced by the combined one. The
// analytic links' node IDs are reported in chain order for stage seven's rewiring; the DeviceMask
// term's parameters are reported for the mask child. Returns false when the terms cannot combine
// (a subtree and a glyph together never lower — the atlas draw's single coverage slot is the
// glyph, so a subtree beside it has no route today).
struct ChainCoverageGraph {
  AOTEffectGraph graph = {};
  AOTNodeID coverageRoot = AOTNodeID::Invalid();
  std::vector<AOTNodeID> clipLinks = {};
  const AOTTextureParameters* mask = nullptr;
  bool coverageLeafFromUVCoord = false;
};

static bool MergeCoverageTerms(const std::vector<AOTCoverageTerm>& terms,
                               ChainCoverageGraph* merged) {
  AOTNodeBuilder builder = {};
  AOTNodeID unit = AOTNodeID::Invalid();
  if (!builder.addGeometryCoverage(&unit)) {
    return false;
  }
  AOTNodeID link = unit;
  for (const auto& term : terms) {
    if (term.kind == AOTCoverageTermKind::Analytic) {
      for (uint32_t index = 1; index < term.graph.nodeCount(); ++index) {
        auto* node = term.graph.nodeAt(AOTNodeID(index));
        AOTNodeID copied = AOTNodeID::Invalid();
        if (node == nullptr || !builder.addCopy(*node, {link}, node->parameters, &copied)) {
          return false;
        }
        link = copied;
        merged->clipLinks.push_back(copied);
      }
    } else if (term.kind == AOTCoverageTermKind::DeviceMask) {
      // The kernel's device-space mask matrix is affine (mat3 without the homogeneous divide), so
      // a perspective mask cannot ride it — the lowering keeps the flag, the compile rejects.
      if (merged->mask != nullptr || term.mask.hasPerspective) {
        return false;
      }
      merged->mask = &term.mask;
    } else {
      if (merged->coverageRoot.isValid() || merged->coverageLeafFromUVCoord) {
        return false;
      }
      // A color glyph replaces the output color, which the chain's coverage composite cannot
      // express (it only modulates alpha): the chain refuses a non-alpha-only glyph leaf and the
      // dedicated atlas kernel carries the color-glyph semantics instead.
      if (term.kind == AOTCoverageTermKind::Glyph) {
        auto* glyphNode = term.graph.nodeAt(term.root);
        auto* glyphParameters = glyphNode != nullptr
                                    ? std::get_if<AOTTextureParameters>(&glyphNode->parameters)
                                    : nullptr;
        if (glyphParameters == nullptr || !glyphParameters->isAlphaOnly) {
          return false;
        }
      }
      std::vector<AOTNodeID> mapped(term.graph.nodeCount(), AOTNodeID::Invalid());
      if (term.graph.nodeCount() == 0 || !term.root.isValid() ||
          term.root.index() >= term.graph.nodeCount()) {
        return false;
      }
      mapped[0] = unit;
      for (uint32_t index = 1; index < term.graph.nodeCount(); ++index) {
        auto* node = term.graph.nodeAt(AOTNodeID(index));
        if (node == nullptr) {
          return false;
        }
        std::vector<AOTNodeID> inputs = {};
        for (auto input : node->inputs) {
          if (input.index() >= mapped.size() || !mapped[input.index()].isValid()) {
            return false;
          }
          inputs.push_back(mapped[input.index()]);
        }
        AOTNodeID copied = AOTNodeID::Invalid();
        if (!builder.addCopy(*node, inputs, node->parameters, &copied)) {
          return false;
        }
        mapped[index] = copied;
      }
      merged->coverageRoot = mapped[term.root.index()];
      merged->coverageLeafFromUVCoord = term.kind == AOTCoverageTermKind::Glyph;
    }
  }
  auto root = merged->coverageRoot.isValid() ? merged->coverageRoot : link;
  return builder.finish(root, &merged->graph);
}

// The one compile entry: merges the draw's coverage terms, runs the chain planner (slot encoding,
// parameter blocks, register allocation) and builds the sampler binding table. Admission and build
// both call this, so an admitted pass is by construction a buildable one — the binding stage can
// only fail on a resource that cannot be described, and that failure surfaces at admission instead
// of at prepare. The table is the single description of the sampler children: it records, in
// artifact order, the DAG leaves, the LUT gradient texture, the noise lookup tables, the phantom
// padding the four-sampler artifacts need and the mask slot child. coverageTerms is null for a
// plan pass (plan passes carry no draw coverage).
bool CompileChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                        const std::vector<AOTCoverageTerm>* coverageTerms, AOTChainLayout* out) {
  if (out == nullptr) {
    return false;
  }
  ChainCoverageGraph coverage = {};
  const AOTEffectGraph* coverageGraph = nullptr;
  if (coverageTerms != nullptr && !coverageTerms->empty()) {
    if (!MergeCoverageTerms(*coverageTerms, &coverage)) {
      return false;
    }
    coverageGraph = &coverage.graph;
  }
  AOTChainLayout layout = {};
  if (!PlanChainLayout(graph, pass, coverageGraph, coverage.coverageRoot,
                       coverage.coverageLeafFromUVCoord, coverage.clipLinks, &layout)) {
    return false;
  }
  auto& program = layout.program;
  std::vector<AOTBinding> bindings = {};
  bindings.reserve(layout.leafNodes.size() + 5);
  for (auto* node : layout.leafNodes) {
    AOTBinding binding = {};
    if (!BuildTextureLeafBinding(node, &binding)) {
      return false;
    }
    bindings.push_back(std::move(binding));
  }
  // A LUT gradient carries its baked texture as a sampler-only binding appended after the DAG
  // leaves; the OP_GRADIENT LUT branch reads it through the GradientLUTLeaf uniform.
  program.lutLeafIndex = -1;
  if (layout.lutGradient != nullptr) {
    if (layout.lutGradient->lutProxy == nullptr) {
      return false;
    }
    SamplingArgs lutArgs = {TileMode::Clamp, TileMode::Clamp,
                            SamplingOptions(FilterMode::Linear, MipmapMode::None),
                            SrcRectConstraint::Fast};
    program.lutLeafIndex = static_cast<int>(bindings.size());
    bindings.push_back({AOTBinding::Kind::Lut, layout.lutGradient->lutProxy, lutArgs, Matrix::I()});
  }
  // TEXTURE_COUNT exists only as 0 or 4: chains with 1-3 sampler children ride the four-leaf
  // artifacts by binding phantom children that re-use the first leaf's texture. Their DAG slots
  // are never OP_TEXTURE, so the kernel's runtime guard never samples them. The phantom must
  // mirror the first leaf's sampler state: on OpenGL the wrap/filter modes are texture-object
  // state (not per-unit), so a default-state phantom bound after the real leaf would overwrite
  // e.g. a decal clamp-to-border with clamp-to-edge for the whole draw.
  std::shared_ptr<TextureProxy> noisePaddingProxy = nullptr;
  if (layout.noise != nullptr &&
      !AppendNoiseLookupBindings(layout.noise, &noisePaddingProxy, &bindings)) {
    return false;
  }
  auto padding =
      ChoosePaddingTexture(graph, pass, coverageGraph, layout.noise, std::move(noisePaddingProxy));
  size_t samplerChildren = layout.leafNodes.size() + (layout.lutGradient != nullptr ? 1 : 0) +
                           (layout.noise != nullptr ? 2 : 0);
  if (!AppendPaddingBindings(padding, &samplerChildren, &bindings) ||
      !AppendMaskBinding(coverage.mask, padding, samplerChildren, &bindings)) {
    return false;
  }
  program.samplerChildCount = static_cast<uint32_t>(samplerChildren);
  program.hasMaskTexture = coverage.mask != nullptr;
  program.noiseSamplerBase =
      layout.noise != nullptr ? static_cast<int>(layout.leafNodes.size()) : -1;
  program.deviceLeaves.reserve(layout.deviceLeafIndices.size());
  for (auto leafIndex : layout.deviceLeafIndices) {
    const auto* parameters = std::get_if<AOTTextureParameters>(
        &layout.leafNodes[static_cast<size_t>(leafIndex)]->parameters);
    if (parameters == nullptr) {
      return false;
    }
    program.deviceLeaves.push_back({leafIndex, parameters->textureProxy, parameters->uvMatrix});
  }
  program.bindings = std::move(bindings);
  *out = std::move(layout);
  return true;
}

// The program-only view of the same entry: every build site that does not keep the leaf facts.
bool CompileChainProgramLayout(const AOTEffectGraph& graph, const PassIR& pass,
                               const std::vector<AOTCoverageTerm>* coverageTerms, AOTProgram* out) {
  if (out == nullptr) {
    return false;
  }
  AOTChainLayout compiled = {};
  if (!CompileChainLayout(graph, pass, coverageTerms, &compiled)) {
    return false;
  }
  *out = std::move(compiled.program);
  return true;
}

}  // namespace tgfx
