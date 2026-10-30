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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProgramWriter.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include "Program.h"
#include "ProgramInfo.h"
#include "core/utils/Log.h"
#include "gpu/AOTChainBuilder.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/EngineParams.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

#if DEBUG
// Prints one bound sampler slot of the TGFX_UNIF_DUMP trace: which texture the slot received, so
// an A/B run can diff the binding order of two routes for the same draw shape.
static void DebugDumpSamplerSlot(const char* label, unsigned slot, const Texture* texture,
                                 bool dummy) {
  if (label == nullptr || texture == nullptr) {
    return;
  }
  std::printf("[UNIF %s] sampler slot=%u texture=%dx%d format=%d%s\n", label, slot,
              texture->width(), texture->height(), static_cast<int>(texture->format()),
              dummy ? " (dummy)" : "");
}

static const char* UnifDumpLabel() {
  static const char* label = std::getenv("TGFX_UNIF_DUMP");
  return label;
}
#endif

static std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
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

static AddressMode ToAddressMode(TileMode tileMode) {
  switch (tileMode) {
    case TileMode::Clamp:
      return AddressMode::ClampToEdge;
    case TileMode::Repeat:
      return AddressMode::Repeat;
    case TileMode::Mirror:
      return AddressMode::MirrorRepeat;
    case TileMode::Decal:
      return AddressMode::ClampToBorder;
  }
  return AddressMode::ClampToEdge;
}

// TextureEffect and TiledTextureEffect must share ONE ordinal sequence: both write the same
// per-texture uniform names (Subset/AlphaOnly/...).
static std::string UniformSpaceKey(const FragmentProcessor* fp) {
  auto name = fp->name();
  return name == "TiledTextureEffect" ? std::string("TextureEffect") : name;
}

bool ProgramWriter::write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const {
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

  beginUniformWrites(vertexUniformData, fragmentUniformData);

  auto expression = buildExpression(info);
  const FragmentProcessor* expressionFP = expression.get();

  auto array = GetRTAdjustArray(info.renderTarget);
  if (vertexUniformData != nullptr) {
    vertexUniformData->setData(RTAdjustName, array);
  }
  writeEngineDefaults(info, fragmentUniformData);

  writeGeometryProcessor(info, vertexUniformData, fragmentUniformData, expressionFP);

  writeProcessors(info, vertexUniformData, fragmentUniformData, expressionFP);

  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, nullptr);

  // TGFX_UNIF_DUMP prints the fully-assembled uniform values (after every writer ran) so an A/B
  // run can diff what the two routes actually upload for the same draw shape. Every draw dumps,
  // labeled by the program key, so two builds can be diffed per draw shape without filtering.
  // Diagnostic only.
#if DEBUG
  const char* unifDump = UnifDumpLabel();
  if (unifDump != nullptr) {
    auto dumpLabel = std::string(unifDump) +
                     " key=" + ProgramInfo::FormatDiagnosticKey(info.programKeyForDiagnostics());
    if (vertexUniformData != nullptr) {
      vertexUniformData->dumpValues(dumpLabel.c_str());
    }
    if (fragmentUniformData != nullptr) {
      fragmentUniformData->dumpValues(dumpLabel.c_str());
    }
  }
#endif

  info.bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                       fragmentOffset);
  writeSamplers(info, program, renderPass, expressionFP);
  return true;
}

ProgramWriter::Expression ProgramWriter::buildExpression(const ProgramInfo&) const {
  return {};
}

void ProgramWriter::beginUniformWrites(UniformData*, UniformData*) const {
}

void ProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void ProgramWriter::writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           const FragmentProcessor* /*expression*/) const {
  FragmentProcessor::CoordTransformIter coordTransformIter(&info);
  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, info.geometryProcessor);
  info.geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);
}

void RuntimeProgramWriter::writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           const FragmentProcessor* /*expression*/) const {
  for (auto& fragmentProcessor : info.fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, fp);
      fp->setData(vertexUniformData, fragmentUniformData);
      fp = iter.next();
    }
  }
  const auto processor = info.getXferProcessor();
  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, processor);
  processor->setData(vertexUniformData, fragmentUniformData);
}

void RuntimeProgramWriter::writeSamplers(const ProgramInfo& info, Program* /*program*/,
                                         RenderPass* renderPass,
                                         const FragmentProcessor* /*expression*/) const {
  auto samplers = info.getSamplers();
  auto gpu = info.renderTarget->getContext()->gpu();
#if DEBUG
  const char* unifDump = UnifDumpLabel();
#endif
  unsigned textureBinding = 0;
  for (auto& [texture, state] : samplers) {
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, texture.get(), false);
#endif
    renderPass->setTexture(textureBinding++, texture, sampler);
  }
}

void PrecompiledProgramWriter::beginUniformWrites(UniformData* vertexUniformData,
                                                  UniformData* fragmentUniformData) const {
  // UniformData is reused across draws, so clear the per-texture structural ordinal left over
  // from a previous draw before writing the base-name uniforms above (RTAdjust / the engine
  // defaults). A stale suffix would misdirect those writes (e.g. tgfx_RTAdjust_1) on the
  // precompiled path.
  if (vertexUniformData != nullptr) {
    vertexUniformData->structuralSuffix = "";
  }
  if (fragmentUniformData != nullptr) {
    fragmentUniformData->structuralSuffix = "";
  }
}

void PrecompiledProgramWriter::writeEngineDefaults(const ProgramInfo& info,
                                                   UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  // Precompiled fill kernels apply the render-target write swizzle at output via this uniform.
  // Only the alpha-only (AAAA) case is non-identity; RGBA targets leave it 0 (no-op). The
  // runtime-generated shader bakes the swizzle into code and does not declare it, so set it
  // optionally. Set in the base (unmangled) context, like RTAdjust.
  int outputAlphaSwizzle = info.getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0;
  fragmentUniformData->setDataOptional(EngineParams::OutputAlphaSwizzle.name, outputAlphaSwizzle);
  fragmentUniformData->setDataOptional(AnalyticClipModule::HasClip.name, 0);
  // Same engine-default treatment as HasClip: only quad_texture_fill declares it, and a present
  // mask FP overwrites it with 1 later in traversal. The color TextureEffect's writer used to
  // carry this default alongside its own fields.
  fragmentUniformData->setDataOptional(DeviceMaskModule::HasDeviceMask.name, 0);
}

void PrecompiledProgramWriter::writeProcessors(const ProgramInfo& info,
                                               UniformData* vertexUniformData,
                                               UniformData* fragmentUniformData,
                                               const FragmentProcessor* /*expression*/) const {
  // Per-processor structural ordinal: on the precompiled path the runtime processor suffix is
  // stripped (skipSuffix), so multiple processors of the same class would write the same
  // base-named per-instance uniforms (a TextureEffect's Subset/AlphaOnly, an
  // XfermodeFragmentProcessor's BlendModeValue, a ConstColorProcessor's ConstColor, ...) and
  // clobber one another. Numbering each processor by its index among same-class processors (in
  // traversal order) gives the precompiled shader stable, predictable names to address them by
  // (Subset / Subset_1, BlendModeValue / BlendModeValue_1, ...). The first processor of each
  // class keeps the base name, so kernels with at most one instance of a class are
  // byte-for-byte unchanged. On the JIT path structuralSuffix is unused (nameSuffix already
  // disambiguates by processor index).
  // A processor whose class overrides onBindPrecompiled resolves to a module Bind that takes over
  // its onSetData at the same walk position: the writes land in the same order, and the
  // structural-suffix addressing (Subset / Subset_1) applies to the module writes unchanged.
  ParamWriter paramWriter(fragmentUniformData);
  std::unordered_map<std::string, int> classOrdinals;
  for (auto& fragmentProcessor : info.fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, fp);
      int ordinal = classOrdinals[UniformSpaceKey(fp)]++;
      std::string structural = ordinal > 0 ? "_" + std::to_string(ordinal) : "";
      if (vertexUniformData != nullptr) {
        vertexUniformData->structuralSuffix = structural;
      }
      if (fragmentUniformData != nullptr) {
        fragmentUniformData->structuralSuffix = structural;
      }
      if (fp->bindPrecompiled(&paramWriter)) {
        // The processor's feature module took over its uniform writes.
      } else {
        fp->setData(vertexUniformData, fragmentUniformData);
      }
      fp = iter.next();
    }
  }
  const auto processor = info.getXferProcessor();
  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, processor);
  if (processor->onBindPrecompiled(&paramWriter)) {
    // The XP's feature module took over its uniform writes.
  } else {
    processor->setData(vertexUniformData, fragmentUniformData);
  }
}

void PrecompiledProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                             RenderPass* renderPass,
                                             const FragmentProcessor* /*expression*/) const {
  auto samplers = info.getSamplers();
  auto expected = program->expectedTextureCount();
  // The dst texture always binds last in the artifact. When the artifact declares more samplers
  // than the draw supplies (e.g. an absent device mask on a runtime-guarded kernel), the holes sit
  // before the dst, so hold the dst aside and pad with the shared dummy texture first; the guarded
  // shader path never samples the padded slots.
  bool holdDst = expected > samplers.size() && !samplers.empty() && info.xferProcessor != nullptr &&
                 info.xferProcessor->dstTextureView() != nullptr;
  size_t sequentialCount = holdDst ? samplers.size() - 1 : samplers.size();
  // The device-mask slot in precompiled kernels sits immediately after the color-side textures
  // (geometry-processor textures plus the color fragment-processor subtrees); the dummy for an
  // absent device mask must occupy that slot, pushing local-mask textures right.
  size_t colorSamplerCount = info.geometryProcessor->numTextureSamplers();
  for (size_t i = 0; i < info.numColorFragmentProcessors(); ++i) {
    FragmentProcessor::Iter colorIter(info.fragmentProcessors[i]);
    const FragmentProcessor* cfp = colorIter.next();
    while (cfp) {
      colorSamplerCount += cfp->numTextureSamplers();
      cfp = colorIter.next();
    }
  }
  unsigned textureBinding = 0;
  auto gpu = info.renderTarget->getContext()->gpu();
  auto* cache = info.renderTarget->getContext()->globalCache();
  // A precompiled kernel can leave a hole right after the supplied color samplers — an absent
  // device mask, or the edge-lit-off UDF's always-declared edge-mask slot in GlassRefractionShader
  // — so an expected/supplied gap means the dummy must be inserted there.
  bool padHole = expected > samplers.size();
#if DEBUG
  const char* unifDump = UnifDumpLabel();
#endif
  for (size_t i = 0; i < sequentialCount; ++i) {
    if (padHole && textureBinding == colorSamplerCount) {
#if DEBUG
      DebugDumpSamplerSlot(unifDump, textureBinding, cache->getOrCreateDummyTexture().get(), true);
#endif
      renderPass->setTexture(textureBinding++, cache->getOrCreateDummyTexture(),
                             cache->getOrCreateDummySampler());
    }
    auto& [texture, state] = samplers[i];
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, texture.get(), false);
#endif
    renderPass->setTexture(textureBinding++, texture, sampler);
  }
  if (textureBinding < expected - (holdDst ? 1u : 0u)) {
    auto dummyTexture = cache->getOrCreateDummyTexture();
    auto dummySampler = cache->getOrCreateDummySampler();
    unsigned stop = expected - (holdDst ? 1u : 0u);
    while (textureBinding < stop) {
#if DEBUG
      DebugDumpSamplerSlot(unifDump, textureBinding, dummyTexture.get(), true);
#endif
      renderPass->setTexture(textureBinding++, dummyTexture, dummySampler);
    }
  }
  if (holdDst) {
    auto& [texture, state] = samplers.back();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, texture.get(), false);
#endif
    renderPass->setTexture(textureBinding, texture, sampler);
  }
}

ProgramWriter::Expression ChainProgramWriter::buildExpression(const ProgramInfo& info) const {
  // Dual form: an info that already carries the chain processor (a multi-pass plan task's pass,
  // until the plan route retires) is its own expression.
  if (info.numColorFragmentProcessors() == 1 && info.numFragmentProcessors() == 1) {
    auto fp = info.getFragmentProcessor(0);
    if (IsProcessor<AOTPointwiseChainProcessor>(fp)) {
      return {{}, fp};
    }
  }
  // The match walk built the expression while accepting the draw; reuse it for this draw's value
  // write instead of rebuilding it.
  if (auto* cached = info.getCachedReexpression()) {
    return {{}, cached};
  }
  auto context = info.getContext();
  if (context == nullptr || context->drawingManager() == nullptr) {
    return {};
  }
  return {AOTChainBuilder::BuildChainProcessorForInfo(context->drawingManager()->drawingAllocator(),
                                                      &info),
          nullptr};
}

void ChainProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                const FragmentProcessor* expression) const {
  // The expression's leaf children own the coordinate transforms in leaf order; the info's own
  // tree orders them differently, so the GP must not consume the tree's iteration.
  FragmentProcessor::CoordTransformIter coordTransformIter(expression);
  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, info.geometryProcessor);
  info.geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);
}

void ExpressionProgramWriter::writeProcessors(const ProgramInfo& info,
                                              UniformData* vertexUniformData,
                                              UniformData* fragmentUniformData,
                                              const FragmentProcessor* expression) const {
  if (expression == nullptr) {
    LOGE(
        "ExpressionProgramWriter::writeProcessors() the expression failed to build; the draw "
        "renders with unwritten kernel uniforms");
    return;
  }
  // The expression's nodes carry the artifact's uniforms (the chain's leaf-ordered per-leaf
  // records, the perlin kernel's folded slot records): walk the expression's subtree — its root
  // first, then every node in its own order — so each node's module bind runs, exactly as the
  // generic writer walks an info that carries the rebuilt processor.
  ParamWriter paramWriter(fragmentUniformData);
  std::unordered_map<std::string, int> classOrdinals;
  FragmentProcessor::Iter iter(expression);
  const FragmentProcessor* fp = iter.next();
  while (fp) {
    info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, fp);
    int ordinal = classOrdinals[UniformSpaceKey(fp)]++;
    std::string structural = ordinal > 0 ? "_" + std::to_string(ordinal) : "";
    if (vertexUniformData != nullptr) {
      vertexUniformData->structuralSuffix = structural;
    }
    if (fragmentUniformData != nullptr) {
      fragmentUniformData->structuralSuffix = structural;
    }
    if (fp->bindPrecompiled(&paramWriter)) {
      // The node's feature module took over its uniform writes.
    } else {
      fp->setData(vertexUniformData, fragmentUniformData);
    }
    fp = iter.next();
  }
  const auto processor = info.getXferProcessor();
  info.updateUniformDataSuffix(vertexUniformData, fragmentUniformData, processor);
  if (processor->onBindPrecompiled(&paramWriter)) {
    // The XP's feature module took over its uniform writes.
  } else {
    processor->setData(vertexUniformData, fragmentUniformData);
  }
}

void ChainProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                       RenderPass* renderPass,
                                       const FragmentProcessor* expression) const {
  if (expression == nullptr) {
    return;
  }
  auto gpu = info.renderTarget->getContext()->gpu();
#if DEBUG
  const char* unifDump = UnifDumpLabel();
#endif
  unsigned textureBinding = 0;
  // The artifact's sampler order is the expression's child order: the DAG leaves (padded to the
  // four-leaf slots), the LUT gradient, the noise lookups, the mask child. Each child binds
  // exactly one texture with its own sampler state.
  for (size_t index = 0; index < expression->numChildProcessors(); ++index) {
    auto child = expression->childProcessor(index);
    if (child->numTextureSamplers() != 1) {
      LOGE("ChainProgramWriter::writeSamplers() unexpected chain child sampler count");
      return;
    }
    auto state = child->samplerStateAt(0);
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, child->textureAt(0).get(), false);
#endif
    renderPass->setTexture(textureBinding++, child->textureAt(0), sampler);
  }
  // The dst texture always binds last.
  auto dstTextureView =
      info.xferProcessor != nullptr ? info.xferProcessor->dstTextureView() : nullptr;
  if (dstTextureView != nullptr) {
    auto state = SamplerState();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, dstTextureView->getTexture().get(), false);
#endif
    renderPass->setTexture(textureBinding++, dstTextureView->getTexture(), sampler);
  }
  DEBUG_ASSERT(textureBinding == program->expectedTextureCount());
}

ProgramWriter::Expression PerlinProgramWriter::buildExpression(const ProgramInfo& info) const {
  // The match walk built the expression while accepting the draw; reuse it for this draw's value
  // write instead of rebuilding it.
  if (auto* cached = info.getCachedReexpression()) {
    return {{}, cached};
  }
  auto context = info.getContext();
  if (context == nullptr || context->drawingManager() == nullptr) {
    return {};
  }
  return {AOTChainBuilder::BuildPerlinProcessorForInfo(
              context->drawingManager()->drawingAllocator(), &info),
          nullptr};
}

}  // namespace tgfx
