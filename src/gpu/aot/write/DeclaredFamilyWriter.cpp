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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cmath>
#include "gpu/CoordTransform.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/Swizzle.h"
#include "gpu/UniformData.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/aot/write/TextureList.h"
#include "gpu/aot/write/modules/LeafWrite.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/Families.h"

namespace tgfx {

namespace {

// MeshGeometry: the view transform, the color pair and the texture coordinate chain.
// ShapeInstancedGeometry: the local-coordinate recovery, the view transform and the sampling
// coordinate. The instance offset and the instance color ride the vertex attributes. The sampling
// transform is the LAST one in the tree: the color graph's transforms come first, so a draw whose
// only fragment processors are the color ones leaves this unwritten (the instanced fill family
// carries no processor at all), and a coverage texture lands on it.
// Gradient: the clamped single-interval gradient's whole record — the endpoints, the boundary
// colors, the layout branch and the conic layout's bias/scale pair. The values are the ones
// BindGradientNode uploads for the legacy instanced-shape kernel; the coordinate transform rides
// the vertex block.
/// The child's sampling fields the blur-style bodies share: the tiled recipe or the plain subset
/// rect, plus the tiled-child selector. Both blur modules declare the same five fields, so the
/// shared body templates on the module and resolves every element index at compile time.
// GlassRefraction: the optics and shape records, sourced from the IR node instead of the
// processor tree. The background-coordinate
// transform is the tree's first coord transform (the standard helper upload).
// HairlineLineGeometry / HairlineQuadGeometry: the view matrix, the color/coverage/AA triple and
// the analytic clip the old declaration carried in the same block.
// AnalyticClip: the runtime clip contract.
/// Writes the analytic clip parameters (slot's module fields) from the draw's IR analytic term.
/// The hairline families carry the same fields inside their geometry module, so the shared body
/// templates on the module that owns the fields.
// Xfer: the blend mode, the dst-texture coord mapping for the dst dialect, and the render-target
// write swizzle — the same values BindPorterDuffXfer uploads for the legacy families.
}  // namespace

/// The declaration-driven families' writer. The variant is fixed at construction: the layout is
/// derived once per program (not once per draw), and every field write addresses the element's
/// index in its module's declaration (a compile-time constant) through the layout's elementField
/// table — no name is composed at draw time. The per-draw work is one match rule (the program's
/// own family, for the data-source leaves) plus the module walk.
class DeclaredFamilyProgramWriter final : public PrecompiledProgramWriter {
 public:
  DeclaredFamilyProgramWriter(const ProgramInfo& info, Variant variant);

  bool canWrite(const ProgramInfo& info) const override;

 protected:
  void writeEngineDefaults(const ProgramInfo& info,
                           UniformData* fragmentUniformData) const override;
  void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                              UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;
  bool writeSamplers(const ProgramInfo& info, Program* program,
                     RenderPass* renderPass) const override;

 private:
  Variant variant;
  Layout layout;
};

DeclaredFamilyProgramWriter::DeclaredFamilyProgramWriter([[maybe_unused]] const ProgramInfo& info,
                                                         Variant variant)
    : variant(std::move(variant)), layout(DeriveLayout(variant)) {
}

bool DeclaredFamilyProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void DeclaredFamilyProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void DeclaredFamilyProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                         UniformData* vertexUniformData,
                                                         UniformData* fragmentUniformData) const {
  // The draw's selection ran once, before the program was created from this variant, and its data
  // sources (the texture leaves, the kernels) rode the info to here: the upload never re-matches.
  const auto* match = info.getSources();
  if (match == nullptr) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw carries no selection for the %s family; uniforms "
        "stay unwritten",
        Families()[variant.family].name);
    return;
  }
  // The program key carries the variant, so a draw landing on this program was selected for the
  // same variant; a mismatch means one key covers two variants and the leaves would write into
  // the wrong layout.
  if (match->variant.id() != variant.id()) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw selected %s variant %u but the program was built "
        "for %u — one program key covers two variants",
        Families()[variant.family].name, match->variant.id(), variant.id());
    return;
  }
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  const auto& geometryParams = info.getGeometryLayout().parameters;
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    SlotWriter writer(layout, *module, slot, &buffer);
    // Every module writes through its own file's entry: the shared writer is down to the walk.
    const auto writeModule = WriterOf(*module);
    DEBUG_ASSERT(writeModule != nullptr);
    if (writeModule != nullptr) {
      WriteContext context = {info, *match, geometryParams, &writer};
      writeModule(context);
    }
  }
}

void DeclaredFamilyProgramWriter::writeProcessors(const ProgramInfo&, UniformData*,
                                                  UniformData*) const {
  // Every value write happens in writeGeometryProcessor's module walk.
}

bool DeclaredFamilyProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                                RenderPass* renderPass) const {
  const auto* sources = info.getSources();
  if (sources == nullptr) {
    LOGE("DeclaredFamilyProgramWriter: the draw carries no selection; nothing binds");
    return false;
  }
  // The declaration is the ordering authority: every module fills the slots it declares, and a
  // slot it declares but the draw does not supply keeps its position as a placeholder.
  TextureList textures(layout);
  const auto& geometryParams = info.getGeometryLayout().parameters;
  WriteContext writeContext = {info, *sources, geometryParams, nullptr, &layout, &textures};
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    if (!BindModuleTextures(writeContext, *module)) {
      return false;
    }
  }
  if (!textures.complete() || textures.size() != program->expectedTextureCount()) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw fills %zu of the %zu declared slots (the program "
        "declares %u); the declaration and the draw's sources disagree",
        textures.size(), layout.textures.size(), program->expectedTextureCount());
    return false;
  }
  auto* drawContext = info.getRenderTarget()->getContext();
  auto gpu = drawContext->gpu();
  auto* cache = drawContext->globalCache();
  const auto& bindings = textures.bindings();
  for (size_t binding = 0; binding < bindings.size(); ++binding) {
    const auto& bound = bindings[binding];
    if (bound.texture == nullptr) {
      renderPass->setTexture(static_cast<unsigned>(binding), cache->getOrCreateDummyTexture(),
                             cache->getOrCreateDummySampler());
      continue;
    }
    const auto& state = bound.state;
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    renderPass->setTexture(static_cast<unsigned>(binding), bound.texture,
                           gpu->createSampler(descriptor));
  }
  return true;
}

std::unique_ptr<ProgramWriter> MakeDeclaredFamilyProgramWriter(const ProgramInfo& info,
                                                               const Variant& variant) {
  return std::make_unique<DeclaredFamilyProgramWriter>(info, variant);
}

}  // namespace tgfx
