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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <utility>
#include <vector>
#include "gpu/ProxyProvider.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/ir/DrawIR.h"

namespace tgfx {

// The binding record of one TextureSource leaf: a plain leaf sampling through its resolved sampler
// state, the stand-in leaf of a device-space source, or a tiled leaf whose wrap modes the kernel's
// single-tap path implements. Only a plain leaf may carry the RGBAAA layout, whose alpha-area
// offset rides the kernel's per-leaf AlphaStart / HasRgbaaa fields. The record carries exactly the
// data the leaf's TextureEffect is constructed from.
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
    // The chain kernel samples this leaf at its DeviceLeafCoordMatrix * gl_FragCoord, so the child
    // only binds the texture with the sampler state of a DeviceSpaceTextureEffect (clamp, linear,
    // no mipmaps). Its coordinate transform is unused. The runtime effect's emission is a bare
    // texture lookup — no subset clamp for any view fact (a smaller-than-proxy view, an External
    // texture) — so this leaf's SubsetClampOn gate must be 0 unconditionally, not follow the
    // subset rule. The unbounded sample area is what forces it; without it, a view-mismatched
    // device leaf would gain a clamp the runtime never emits.
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
    binding->tiledRecipe = recipe;
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

// Merges the draw's coverage terms into ONE coverage graph for the layout (the contract and the
// shape rules live in AOTChainInternal.h): one geometry-coverage unit, the analytic terms' links
// chained onto it, and — when a subtree or glyph term exists — its nodes copied over with the
// term's own unit replaced by the combined one.
bool MergeCoverageTerms(const std::vector<AOTCoverageTerm>& terms, ChainCoverageGraph* merged) {
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

// The pass's resource demand over the laid-out color nodes: the leaf bindings (the leaf records,
// where a multi-plane source owns three), the LUT gradient's baked texture and the noise source's
// pair of lookup tables. Shader-side tiled recipes cannot reach here (the layout declines them),
// so the demand carries none.
static AOTResourceDemand ChainDemandOf(const AOTChainGraphLayout& layout) {
  AOTResourceDemand demand = {};
  demand.leafBindings = layout.leafNodes.size() + (layout.lutGradient != nullptr ? 1 : 0);
  for (size_t index = 0; index < layout.ordered.size(); ++index) {
    const size_t combined = layout.ordered[index];
    if (combined >= layout.colorCount) {
      continue;
    }
    const auto* node = layout.nodes[combined];
    if (node->kind == AOTEffectKind::GradientSource) {
      const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters);
      if (gradient != nullptr) {
        ++demand.gradients;
      }
    }
  }
  demand.instructions = layout.instructionCount;
  demand.noiseSources = layout.noise != nullptr ? 1 : 0;
  demand.leafBindings += demand.noiseSources * 2;
  return demand;
}

// The pass's final profile: the declared one when the caller's plan resolved it, else the smallest
// class the demand fits. The draw's coverage may only raise a plain profile onto the gradient
// class (a coverage gradient moves a Chain pass onto the TiledGraph family, whose budgets are a
// superset for those fields).
static bool ResolveChainProfile(const AOTChainGraphLayout& layout, const PassIR* pass,
                                AOTResourceProfile* profile) {
  const auto demand = ChainDemandOf(layout);
  auto resolved = pass != nullptr ? pass->resourceProfile : AOTResourceProfile::Automatic;
  if (resolved == AOTResourceProfile::Automatic) {
    resolved = SelectAOTResourceProfile(demand);
  }
  if (!FitsAOTResourceProfile(demand, resolved)) {
    return false;
  }
  if (resolved == AOTResourceProfile::Chain && demand.gradients > 0 &&
      layout.coverageGraph != nullptr) {
    resolved = AOTResourceProfile::TiledGraph;
  }
  *profile = resolved;
  return true;
}

bool ChainProgramFits(const DrawIR& ir, const PassIR& pass, AOTResourceProfile* outProfile) {
  AOTChainGraphLayout layout = {};
  if (!LayoutChainGraph(ir, &pass, &layout)) {
    return false;
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!ResolveChainProfile(layout, &pass, &profile)) {
    return false;
  }
  if (outProfile != nullptr) {
    *outProfile = profile;
  }
  return true;
}

std::vector<CoordTransform> BuildChainCoordTransforms(const DrawIR& ir, const AOTProgram& program) {
  std::vector<CoordTransform> transforms = {};
  if (IsAOTGraphProfile(program.profile)) {
    // The chain-wide source matrix: the gradient's coordinate matrix or the noise source's —
    // either graph (color or coverage) may carry the one source the profile was resolved for.
    CoordTransform source = {};
    const auto scanGraph = [&source](const AOTEffectGraph& graph) {
      for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
        auto* node = graph.nodeAt(AOTNodeID(index));
        if (node == nullptr) {
          continue;
        }
        if (auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters)) {
          source = CoordTransform(gradient->coordMatrix);
        } else if (auto* noise = std::get_if<AOTPerlinNoiseParameters>(&node->parameters)) {
          source = CoordTransform(noise->uvMatrix);
        }
      }
    };
    scanGraph(ir.color);
    for (const auto& term : ir.coverage) {
      scanGraph(term.graph);
    }
    transforms.push_back(source);
  }
  for (const auto& binding : program.bindings) {
    // The device mask samples at its DeviceLeafCoordMatrix * gl_FragCoord, so the device-space
    // effect registers no transform; the phantom mask slot is a plain texture child and does.
    if (binding.kind == AOTBinding::Kind::Mask) {
      continue;
    }
    transforms.emplace_back(binding.uvMatrix, binding.textureProxy.get(), binding.alphaStart);
  }
  return transforms;
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

// Picks the padding texture by walking the laid-out color nodes (the layout already holds the
// pass's own node set) and the coverage subtree. Falls back to the noise tables' first proxy with
// nearest sampling.
static ChainPaddingTexture ChoosePaddingTexture(const AOTChainGraphLayout& layout,
                                                std::shared_ptr<TextureProxy> noisePaddingProxy) {
  ChainPaddingTexture padding = {};
  for (size_t index = 0; index < layout.ordered.size(); ++index) {
    const size_t combined = layout.ordered[index];
    if (combined < layout.colorCount) {
      ConsiderPaddingNode(layout.nodes[combined], &padding);
    }
  }
  for (size_t index = 0; index < layout.covCount; ++index) {
    ConsiderPaddingNode(layout.nodes[layout.colorCount + index], &padding);
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

// The one build entry over the graph layout: the binding table (the DAG leaves, the LUT gradient
// texture, the noise lookup tables, the phantom padding the four-sampler artifacts need and the
// mask slot child) plus the leaf facts the writer and the variant derivation read. Admission and
// the build run the same derivation, so an admitted pass is by construction a buildable one — the
// binding stage can only fail on a resource that cannot be described, and that failure surfaces
// at admission instead of at prepare.
bool BuildChainProgram(const DrawIR& ir, const PassIR* pass, AOTProgram* out) {
  if (out == nullptr) {
    return false;
  }
  AOTChainGraphLayout layout = {};
  if (!LayoutChainGraph(ir, pass, &layout)) {
    return false;
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!ResolveChainProfile(layout, pass, &profile)) {
    return false;
  }
  AOTProgram program = {};
  program.profile = profile;
  program.rootRegister = layout.rootIndex;
  program.coverageRootRegister = layout.coverageRootSlot;
  program.clipCoverageRegister = layout.clipCoverageSlot;
  program.coordSourceMask = layout.coordSourceMask;
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
  // leaves; the operator's LUT branch reads it through the GradientLutLeaf uniform.
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
  // artifacts by binding phantom children that re-use the first leaf's texture. The phantom must
  // mirror the first leaf's sampler state: on OpenGL the wrap/filter modes are texture-object
  // state (not per-unit), so a default-state phantom bound after the real leaf would overwrite
  // e.g. a decal clamp-to-border with clamp-to-edge for the whole draw.
  std::shared_ptr<TextureProxy> noisePaddingProxy = nullptr;
  if (layout.noise != nullptr &&
      !AppendNoiseLookupBindings(layout.noise, &noisePaddingProxy, &bindings)) {
    return false;
  }
  auto padding = ChoosePaddingTexture(layout, std::move(noisePaddingProxy));
  size_t samplerChildren = layout.leafNodes.size() + (layout.lutGradient != nullptr ? 1 : 0) +
                           (layout.noise != nullptr ? 2 : 0);
  if (!AppendPaddingBindings(padding, &samplerChildren, &bindings) ||
      !AppendMaskBinding(layout.coverage.mask, padding, samplerChildren, &bindings)) {
    return false;
  }
  program.samplerChildCount = static_cast<uint32_t>(samplerChildren);
  program.hasMaskTexture = layout.coverage.mask != nullptr;
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
  *out = std::move(program);
  return true;
}

}  // namespace tgfx
