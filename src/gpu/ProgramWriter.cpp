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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProgramWriter.h"
#include "Program.h"
#include "ProgramInfo.h"
#include "core/utils/Log.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/aot/write/TextureList.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/Chain.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(renderTarget->width());
  result[2] = 2.f / static_cast<float>(renderTarget->height());
  result[1] = -1.f;
  result[3] = -1.f;
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  // WebGPU viewport Y transform is (1 - y_ndc)/2, meaning NDC y=+1 maps to framebuffer top.
  // For TopLeft origin (pixel y=0 at top), we need pixel y=0 -> NDC y=+1, requiring Y negate.
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

ProgramWriter::ProgramWriter(Variant variant)
    : variant(std::move(variant)), layout(DeriveLayout(this->variant)) {
}

bool ProgramWriter::carriesUniformContent(const ProgramInfo& info) const {
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    if (modules.list[slot] != &Chain) {
      continue;
    }
    // The interpreter's writers read the compiled program the selection attached; without it the
    // draw would submit with unwritten fields.
    const auto* sources = info.getSources();
    return sources != nullptr && sources->chain != nullptr;
  }
  return true;
}

bool ProgramWriter::write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const {
  if (!carriesUniformContent(info)) {
    LOGE("ProgramWriter::write() the program's uniform content is unavailable; skipping the draw");
    return false;
  }
  size_t vertexOffset = 0;
  size_t fragmentOffset = 0;
  auto uniformBuffer = info.getUniformBuffer(program, &vertexOffset, &fragmentOffset);

  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);
  if (uniformBuffer == nullptr &&
      (vertexUniformData != nullptr || fragmentUniformData != nullptr)) {
    // The program needs a uniform block but no backing store is available (allocation or map
    // failed). Aborting the draw loses one frame's content; uploading anyway would write through
    // a null pointer. Tell the caller to skip the draw.
    LOGE("ProgramWriter::write() no uniform buffer available; skipping the draw");
    return false;
  }

  writeUniforms(info, vertexUniformData, fragmentUniformData);

  info.bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                       fragmentOffset);
  return writeSamplers(info, program, renderPass);
}

void ProgramWriter::writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                                  UniformData* fragmentUniformData) const {
  // The draw's selection ran once, before the program was created from this variant, and its data
  // sources (the texture leaves, the kernels, the compiled interpreter program) rode the info to
  // here: the upload never re-matches.
  const auto* sources = info.getSources();
  if (sources == nullptr) {
    LOGE("ProgramWriter: the draw carries no selection for the %s family; uniforms stay unwritten",
         Families()[variant.family].name);
    return;
  }
  // The program key carries the variant, so a draw landing on this program was selected for the
  // same variant; a mismatch means one key covers two variants and the writers would address the
  // wrong layout.
  if (sources->variant.id() != variant.id()) {
    LOGE(
        "ProgramWriter: the draw selected %s variant %u but the program was built for %u — one "
        "program key covers two variants",
        Families()[variant.family].name, sources->variant.id(), variant.id());
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
      WriteContext context = {info, *sources, geometryParams, &writer};
      writeModule(context);
    }
  }
}

bool ProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                  RenderPass* renderPass) const {
  const auto* sources = info.getSources();
  if (sources == nullptr) {
    LOGE("ProgramWriter: the draw carries no selection; nothing binds");
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
        "ProgramWriter: the draw fills %zu of the %zu declared slots (the program declares %u); "
        "the declaration and the draw's sources disagree",
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

}  // namespace tgfx
