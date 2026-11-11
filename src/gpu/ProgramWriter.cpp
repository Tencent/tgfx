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
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/ColorSpaceParams.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/EngineParams.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// The compiled YUV tail program, mirroring ChainProgram: kept on the draw's IR, compiled here
// once when the match never needed to (the family's matching reads IR facts only).

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

bool ProgramWriter::write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const {
  if (!canWrite(info)) {
    // The route's compiled program is unavailable: submitting would draw with unwritten (or
    // garbage) uniform fields. Tell the caller to skip the draw.
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

  writeUniformValues(info, vertexUniformData, fragmentUniformData);

  info.bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                       fragmentOffset);
  writeSamplers(info, program, renderPass);
  return true;
}

bool ProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void ProgramWriter::writeUniformValues(const ProgramInfo& info, UniformData* vertexUniformData,
                                       UniformData* fragmentUniformData) const {
  auto array = GetRTAdjustArray(info.renderTarget);
  if (vertexUniformData != nullptr) {
    // The legacy kernels spell the vector through EngineParams ("tgfx_RTAdjust"); the declared
    // families' Frame module spell it "RTAdjust". Write whichever spelling the program carries —
    // both are optional so the other one is a silent no-op instead of a log line per draw.
    vertexUniformData->setDataOptional(EngineParams::RTAdjust.name, array);
    vertexUniformData->setDataOptional("RTAdjust", array);
  }
  writeEngineDefaults(info, fragmentUniformData);

  writeGeometryProcessor(info, vertexUniformData, fragmentUniformData);

  writeProcessors(info, vertexUniformData, fragmentUniformData);
}

void ProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void ProgramWriter::writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData) const {
  FragmentProcessor::CoordTransformIter coordTransformIter(&info);
  info.geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);
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
  int zero = 0;
  if (!fragmentUniformData->writeParamKey(&EngineParams::OutputAlphaSwizzle, 0, &outputAlphaSwizzle,
                                          sizeof(outputAlphaSwizzle))) {
    fragmentUniformData->setDataOptional(EngineParams::OutputAlphaSwizzle.name, outputAlphaSwizzle);
  }
  if (!fragmentUniformData->writeParamKey(&AnalyticClipModule::HasClip, 0, &zero, sizeof(zero))) {
    fragmentUniformData->setDataOptional(AnalyticClipModule::HasClip.name, zero);
  }
  // Same engine-default treatment as HasClip: only quad_texture_fill declares it, and a present
  // mask FP overwrites it with 1 later in traversal. The color TextureEffect's writer used to
  // carry this default alongside its own fields.
  if (!fragmentUniformData->writeParamKey(&DeviceMaskModule::HasDeviceMask, 0, &zero,
                                          sizeof(zero))) {
    fragmentUniformData->setDataOptional(DeviceMaskModule::HasDeviceMask.name, zero);
  }
}

void PrecompiledProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                             RenderPass* renderPass) const {
  auto samplers = info.getSamplers();
  auto expected = program->expectedTextureCount();
  // The dst texture always binds last in the artifact. When the artifact declares more samplers
  // than the draw supplies (e.g. an absent device mask on a runtime-guarded kernel), the holes sit
  // before the dst, so hold the dst aside and pad with the shared dummy texture first; the guarded
  // shader path never samples the padded slots.
  const auto& xferDstProxy = info.getXferFacts().dstTexture.textureProxy;
  auto xferDstView = xferDstProxy != nullptr ? xferDstProxy->getTextureView() : nullptr;
  bool holdDst = expected > samplers.size() && !samplers.empty() && xferDstView != nullptr;
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
  // device mask, or the edge-lit-off UDF's always-declared edge-mask slot in the declared
  // GlassRefraction families — so an expected/supplied gap means the dummy must be inserted there.
  bool padHole = expected > samplers.size();
  for (size_t i = 0; i < sequentialCount; ++i) {
    if (padHole && textureBinding == colorSamplerCount) {
      renderPass->setTexture(textureBinding++, cache->getOrCreateDummyTexture(),
                             cache->getOrCreateDummySampler());
    }
    auto& [texture, state] = samplers[i];
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, texture, sampler);
  }
  if (textureBinding < expected - (holdDst ? 1u : 0u)) {
    auto dummyTexture = cache->getOrCreateDummyTexture();
    auto dummySampler = cache->getOrCreateDummySampler();
    unsigned stop = expected - (holdDst ? 1u : 0u);
    while (textureBinding < stop) {
      renderPass->setTexture(textureBinding++, dummyTexture, dummySampler);
    }
  }
  if (holdDst) {
    auto& [texture, state] = samplers.back();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding, texture, sampler);
  }
}

bool PrecompiledProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

// The compiled chain program this draw resolves to comes from the one memoized entry
// (AOTChainBuilder::ResolveChainProgram): a plan pass carries it on its IR, the first match or
// write compiles it there, and every later caller reads it back — the transform, uniform and
// sampler writes share one description, and a repeated prepare never recompiles.

bool ChainProgramWriter::canWrite(const ProgramInfo& info) const {
  // The chain kernel's whole uniform content, transforms and sampler order come from the compiled
  // program; without it there is nothing correct to upload.
  return AOTChainBuilder::ResolveChainProgram(&info) != nullptr;
}

void ChainProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData) const {
  const auto* program = AOTChainBuilder::ResolveChainProgram(&info);
  if (program == nullptr) {
    LOGE(
        "ChainProgramWriter::writeGeometryProcessor() no compiled chain program; the draw renders "
        "without its coordinate transforms");
    return;
  }
  // The program's binding table describes the artifact's transforms in leaf order; the info's own
  // processor tree orders them differently, so the GP must not consume the tree's iteration.
  auto transforms = BuildChainCoordTransforms(*program);
  FragmentProcessor::CoordTransformIter coordTransformIter(&transforms);
  info.geometryProcessor->setData(vertexUniformData, fragmentUniformData, &coordTransformIter);
}

void ChainProgramWriter::writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                                         UniformData* fragmentUniformData) const {
  const auto* program = AOTChainBuilder::ResolveChainProgram(&info);
  if (program == nullptr) {
    LOGE(
        "ChainProgramWriter::writeProcessors() no compiled chain program; the draw renders with "
        "unwritten kernel uniforms");
    return;
  }
  // The shell-free value write: the draw's whole uniform content comes from the compiled program —
  // the root block plus every binding's per-child records — so no processor tree is walked. The
  // xfer processor still writes through its own bind until the xfer contract lands.
  BindChainProgram(*program, vertexUniformData, fragmentUniformData);
  BindChainBindings(*program, fragmentUniformData);
  ParamWriter paramWriter(fragmentUniformData);
  const auto& xfer = info.getXferFacts();
  const auto* chainIR = info.getCarriedIR();
  if (chainIR != nullptr && xfer.shaderBlend) {
    BindPorterDuffXfer(static_cast<int>(chainIR->blendMode), xfer.dstTexture, &paramWriter);
  }
}

void ChainProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                       RenderPass* renderPass) const {
  // `program` is read by DEBUG_ASSERT only, which Release builds compile out.
  (void)program;
  const auto* chainProgram = AOTChainBuilder::ResolveChainProgram(&info);
  if (chainProgram == nullptr) {
    LOGE("ChainProgramWriter::writeSamplers() no compiled chain program; skipping the bindings");
    return;
  }
  auto gpu = info.renderTarget->getContext()->gpu();
  unsigned textureBinding = 0;
  // The artifact's sampler order is the program's binding order: the DAG leaves (padded to the
  // four-leaf slots), the LUT gradient, the noise lookups, then the mask slot child. Each binding
  // carries the texture and the sampling its sampler state is built from.
  for (const auto& binding : chainProgram->bindings) {
    auto textureView =
        binding.textureProxy != nullptr ? binding.textureProxy->getTextureView() : nullptr;
    if (textureView == nullptr || textureView->getTexture() == nullptr) {
      LOGE("ChainProgramWriter::writeSamplers() a bound texture is not instantiated");
      return;
    }
    SamplerState state(binding.args.tileModeX, binding.args.tileModeY, binding.args.sampling);
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, textureView->getTexture(), sampler);
  }
  // The dst texture always binds last.
  const auto& dstProxy = info.getXferFacts().dstTexture.textureProxy;
  auto dstTextureView = dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
  if (dstTextureView != nullptr) {
    auto state = SamplerState();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, dstTextureView->getTexture(), sampler);
  }
  if (textureBinding != program->expectedTextureCount()) {
    LOGE("ChainProgramWriter::writeSamplers(): bound %u textures but the program declares %zu",
         textureBinding, program->expectedTextureCount());
    DEBUG_ASSERT(false);
  }
}

}  // namespace tgfx
