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
  // Read per call, not cached: the A/B harness toggles the variable around each route within one
  // process, and a cached first read would freeze whichever route ran first.
  return std::getenv("TGFX_UNIF_DUMP");
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

  // TGFX_UNIF_DUMP prints the fully-assembled uniform values (after every writer ran) so an A/B
  // run can diff what the two routes actually upload for the same draw shape. Every draw dumps,
  // labeled by the program key, so two builds can be diffed per draw shape without filtering.
  // Diagnostic only.
#if DEBUG
  const char* unifDump = UnifDumpLabel();
  if (unifDump != nullptr) {
    auto dumpLabel =
        std::string(unifDump) + " key=" + ProgramInfo::FormatDiagnosticKey(info.programKey());
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
    vertexUniformData->setData(EngineParams::RTAdjust.name, array);
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

// Consumes the first unconsumed leaf whose proxy matches, running the bind on it. The texture
// leaves are matched by proxy identity rather than walk order so a repeated source (the same
// image twice) stays correct.
template <typename Bind>
static bool TakeLeafByProxy(std::vector<const AOTTextureParameters*>& leaves,
                            const std::shared_ptr<TextureProxy>& proxy, Bind bind) {
  for (auto*& candidate : leaves) {
    if (candidate != nullptr && candidate->textureProxy == proxy) {
      bind(*candidate);
      candidate = nullptr;
      return true;
    }
  }
  return false;
}

// TextureEffect and TiledTextureEffect must share ONE ordinal sequence: both write the same
// per-texture uniform names (Subset/AlphaOnly/...).
static std::string UniformSpaceKey(const FragmentProcessor* fp) {
  auto name = fp->name();
  return name == "TiledTextureEffect" ? std::string("TextureEffect") : name;
}

// The tree walk (the S4.4 form): the IR facts bind per processor as the walk visits it, and the
// processors no IR fact covers -- the glass family's opaque kernels -- keep their setData writes.
// It serves the IRs the action list cannot yet: anything carrying a non-gaussian opaque kernel.
void PrecompiledProgramWriter::writeProcessorsByTree(const ProgramInfo& info,
                                                     UniformData* vertexUniformData,
                                                     UniformData* fragmentUniformData) const {
  ParamWriter paramWriter(fragmentUniformData);
  const auto* carriedIR = info.getCarriedIR();
  // The carried IR is the lowered form of this very processor tree, so every module-bound
  // processor reads its values off an IR fact instead of the live object: the texture leaves
  // and the device mask are matched by proxy identity, the clip / pointwise / gradient / blur
  // facts by kind in walk order (the order the lowering recorded them, which is the order this
  // walk visits the processors). A missing fact cannot occur on a served draw -- the IR is this
  // tree's lowering -- and the setData fallback below stays loud if it ever does. What remains
  // on the live tree are the setData-based writers (the xfer-mode FP's blend mode, the
  // color-space helper, the processors that write nothing at all).
  std::vector<const AOTTextureParameters*> plainLeaves = {};
  std::vector<const AOTTextureParameters*> tiledLeaves = {};
  std::vector<const AOTTextureParameters*> deviceMaskLeaves = {};
  std::vector<const AOTRectCoverageParameters*> rectClips = {};
  std::vector<const AOTRRectCoverageParameters*> rrectClips = {};
  std::vector<const AOTColorMatrixParameters*> colorMatrices = {};
  std::vector<const ChainISA::LumaData*> lumas = {};
  std::vector<float> alphaThresholds = {};
  std::vector<const AOTGradientParameters*> gradients = {};
  std::vector<std::pair<const AOTOpaqueKernelParameters*, const AOTTextureParameters*>> blurs = {};
  if (carriedIR != nullptr) {
    // The graph stores its nodes in build (post-)order -- children before the parents that
    // consume them -- while the walk below visits the tree pre-order, so the cursor-matched
    // kinds must be collected in the walk's own order: a recursive descent from the root.
    const auto collectGraph = [&](const AOTEffectGraph& graph, auto&& self,
                                  AOTNodeID nodeID) -> void {
      const auto* node = graph.nodeAt(nodeID);
      {
        switch (node->kind) {
          case AOTEffectKind::TextureSource: {
            auto* leaf = std::get_if<AOTTextureParameters>(&node->parameters);
            if (leaf == nullptr || leaf->textureProxy == nullptr) {
              break;
            }
            if (leaf->samplingKind == AOTTextureSamplingKind::Device) {
              deviceMaskLeaves.push_back(leaf);
            } else if (leaf->tiledRecipe.has_value()) {
              tiledLeaves.push_back(leaf);
            } else if (leaf->samplingKind == AOTTextureSamplingKind::Plain) {
              plainLeaves.push_back(leaf);
            }
            break;
          }
          case AOTEffectKind::ColorMatrix:
            if (auto* params = std::get_if<AOTColorMatrixParameters>(&node->parameters)) {
              colorMatrices.push_back(params);
            }
            break;
          case AOTEffectKind::Luma:
            if (auto* params = std::get_if<ChainISA::LumaData>(&node->parameters)) {
              lumas.push_back(params);
            }
            break;
          case AOTEffectKind::AlphaThreshold:
            if (auto* params = std::get_if<ChainISA::AlphaThresholdData>(&node->parameters)) {
              alphaThresholds.push_back(params->threshold);
            }
            break;
          case AOTEffectKind::GradientSource:
            if (auto* params = std::get_if<AOTGradientParameters>(&node->parameters)) {
              gradients.push_back(params);
            }
            break;
          case AOTEffectKind::RectCoverage:
            if (auto* params = std::get_if<AOTRectCoverageParameters>(&node->parameters)) {
              rectClips.push_back(params);
            }
            break;
          case AOTEffectKind::RRectCoverage:
            if (auto* params = std::get_if<AOTRRectCoverageParameters>(&node->parameters)) {
              rrectClips.push_back(params);
            }
            break;
          case AOTEffectKind::OpaqueKernel: {
            auto* params = std::get_if<AOTOpaqueKernelParameters>(&node->parameters);
            const AOTTextureParameters* child = nullptr;
            if (params != nullptr && !node->inputs.empty()) {
              const auto* childNode = graph.nodeAt(node->inputs[0]);
              if (childNode != nullptr && childNode->kind == AOTEffectKind::TextureSource) {
                child = std::get_if<AOTTextureParameters>(&childNode->parameters);
              }
            }
            if (params != nullptr) {
              blurs.emplace_back(params, child);
            }
            break;
          }
          default:
            break;
        }
        for (const auto& input : node->inputs) {
          self(graph, self, input);
        }
      }
    };
    const auto collectRooted = [&](const AOTEffectGraph& graph) {
      if (graph.root().isValid()) {
        collectGraph(graph, collectGraph, graph.root());
      }
    };
    collectRooted(carriedIR->color);
    for (const auto& term : carriedIR->coverage) {
      switch (term.kind) {
        case AOTCoverageTermKind::DeviceMask:
          if (term.mask.textureProxy != nullptr) {
            deviceMaskLeaves.push_back(&term.mask);
          }
          break;
        case AOTCoverageTermKind::Analytic:
        case AOTCoverageTermKind::Subtree:
        case AOTCoverageTermKind::Glyph:
          if (term.root.isValid()) {
            collectGraph(term.graph, collectGraph, term.root);
          }
          break;
      }
    }
  }
  std::unordered_map<std::string, int> classOrdinals;
  size_t rectCursor = 0;
  size_t rrectCursor = 0;
  size_t colorMatrixCursor = 0;
  size_t lumaCursor = 0;
  size_t alphaThresholdCursor = 0;
  size_t gradientCursor = 0;
  size_t blurCursor = 0;
  for (auto& fragmentProcessor : info.fragmentProcessors) {
    FragmentProcessor::Iter iter(fragmentProcessor);
    const FragmentProcessor* fp = iter.next();
    while (fp) {
      int ordinal = classOrdinals[UniformSpaceKey(fp)]++;
      std::string structural = ordinal > 0 ? "_" + std::to_string(ordinal) : "";
      paramWriter.setInstanceOrdinal(static_cast<size_t>(ordinal));
      bool bound = false;
      if (IsProcessor<TextureEffect>(fp)) {
        auto proxy = static_cast<const TextureEffect*>(fp)->getTextureProxy();
        bound = TakeLeafByProxy(plainLeaves, proxy, [&](const AOTTextureParameters& leaf) {
          BindTextureSourceLeaf(leaf, &paramWriter);
        });
      } else if (IsProcessor<TiledTextureEffect>(fp)) {
        auto proxy = static_cast<const TiledTextureEffect*>(fp)->getTextureProxy();
        bound = TakeLeafByProxy(tiledLeaves, proxy, [&](const AOTTextureParameters& leaf) {
          BindTiledSourceLeaf(leaf, &paramWriter);
        });
      } else if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
        auto proxy = static_cast<const DeviceSpaceTextureEffect*>(fp)->getTextureProxy();
        bound = TakeLeafByProxy(deviceMaskLeaves, proxy, [&](const AOTTextureParameters& leaf) {
          BindDeviceMaskLeaf(leaf, &paramWriter);
        });
      } else if (IsProcessor<RectEffect>(fp)) {
        if (rectCursor < rectClips.size()) {
          BindRectClipNode(*rectClips[rectCursor++], &paramWriter);
          bound = true;
        }
      } else if (IsProcessor<RRectEffect>(fp)) {
        if (rrectCursor < rrectClips.size()) {
          BindRRectClipNode(*rrectClips[rrectCursor++], &paramWriter);
          bound = true;
        }
      } else if (IsProcessor<ColorMatrixFragmentProcessor>(fp)) {
        if (colorMatrixCursor < colorMatrices.size()) {
          AOTChainSlot slot = {};
          slot.op = AOTChainOp::ColorMatrix;
          slot.colorMatrix = *colorMatrices[colorMatrixCursor++];
          UploadPointwiseSlot(fragmentUniformData, slot);
          bound = true;
        }
      } else if (IsProcessor<LumaFragmentProcessor>(fp)) {
        if (lumaCursor < lumas.size()) {
          AOTChainSlot slot = {};
          slot.op = AOTChainOp::Luma;
          slot.luma = *lumas[lumaCursor++];
          UploadPointwiseSlot(fragmentUniformData, slot);
          bound = true;
        }
      } else if (IsProcessor<AlphaThresholdFragmentProcessor>(fp)) {
        if (alphaThresholdCursor < alphaThresholds.size()) {
          AOTChainSlot slot = {};
          slot.op = AOTChainOp::AlphaThreshold;
          slot.alphaThreshold = {alphaThresholds[alphaThresholdCursor++]};
          UploadPointwiseSlot(fragmentUniformData, slot);
          bound = true;
        }
      } else if (IsProcessor<ClampedGradientEffect>(fp)) {
        // The merged node writes the effect's fields and, gated by its own discriminators, the
        // conic layout's and the single-interval colorizer's -- everything the three separate
        // processors used to bind on their own visits.
        if (gradientCursor < gradients.size()) {
          BindGradientNode(*gradients[gradientCursor++], &paramWriter);
          bound = true;
        }
      } else if (IsProcessor<GaussianBlur1DFragmentProcessor>(fp)) {
        if (blurCursor < blurs.size()) {
          const auto& blur = blurs[blurCursor++];
          BindBlurKernelNode(*blur.first, blur.second, &paramWriter);
          bound = true;
        }
      }
      if (!bound) {
        // A setData-based writer, or the impossible missing-IR case (kept loud: the required
        // uniform lookup logs when the name does not resolve).
        fp->setData(vertexUniformData, fragmentUniformData);
      }
      fp = iter.next();
    }
  }
  const auto processor = info.getXferProcessor();
  // The offset path's instance ordinal for the XP follows the base names.
  paramWriter.setInstanceOrdinal(0);
  if (carriedIR != nullptr && ClassifyXferForMatch(processor) != XferKind::None) {
    // The blend mode off the IR; the dst texture's instance-time facts off the made processor --
    // the same layer the geometry facts read.
    BindPorterDuffXfer(static_cast<int>(carriedIR->blendMode), processor, &paramWriter);
  } else {
    processor->setData(vertexUniformData, fragmentUniformData);
  }
}

void PrecompiledProgramWriter::writeProcessors(const ProgramInfo& info,
                                               UniformData* vertexUniformData,
                                               UniformData* fragmentUniformData) const {
  ParamWriter paramWriter(fragmentUniformData);
  const auto* carriedIR = info.getCarriedIR();
  if (carriedIR == nullptr) {
    // A served draw always carries its IR (the match read it); this is the defensive hole.
    return;
  }
  // One bind action per IR fact, collected in the walk's own pre-order (the graph stores its
  // nodes post-order), so the per-class instance ordinals the kernel's fields address (Subset /
  // Subset_1, BlendModeValue / BlendModeValue_1) keep the numbering the processor walk used to
  // compute. The whole upload reads the IR -- no FragmentProcessor is consulted anywhere.
  struct Action {
    enum class Kind {
      PlainLeaf,
      TiledLeaf,
      DeviceMask,
      RectClip,
      RRectClip,
      Pointwise,
      Gradient,
      Blur,
    } kind = Kind::PlainLeaf;
    size_t ordinal = 0;
    const AOTTextureParameters* leaf = nullptr;
    const AOTTextureParameters* blurChild = nullptr;
    const AOTRectCoverageParameters* rectClip = nullptr;
    const AOTRRectCoverageParameters* rrectClip = nullptr;
    // One pointwise operator as a chain slot record: the operator value plus its parameter block.
    // The upload writes it through Binder::UploadPointwiseSlot, the same encoding the chain's
    // slots use, so a dedicated single-operator kernel and a chain share one parameter ABI.
    AOTChainSlot pointwise = {};
    const AOTGradientParameters* gradient = nullptr;
    const AOTOpaqueKernelParameters* blur = nullptr;
  };
  std::vector<Action> actions = {};
  size_t textureOrdinal = 0;
  // The glass family's opaque kernels (kind 1/2) still upload through their processors'
  // setData; an IR carrying one takes the tree walk instead of the action list.
  bool hasProcessorBoundOpaqueKernel = false;
  const auto collect = [&](const AOTEffectGraph& graph, auto&& self, AOTNodeID nodeID) -> void {
    const auto* node = graph.nodeAt(nodeID);
    Action action = {};
    bool resolved = false;
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
        auto* leaf = std::get_if<AOTTextureParameters>(&node->parameters);
        if (leaf == nullptr || leaf->textureProxy == nullptr) {
          break;
        }
        if (leaf->samplingKind == AOTTextureSamplingKind::Device) {
          action.kind = Action::Kind::DeviceMask;
        } else if (leaf->tiledRecipe.has_value()) {
          action.kind = Action::Kind::TiledLeaf;
          action.ordinal = textureOrdinal++;
        } else if (leaf->samplingKind == AOTTextureSamplingKind::Plain) {
          action.kind = Action::Kind::PlainLeaf;
          action.ordinal = textureOrdinal++;
        } else {
          break;
        }
        action.leaf = leaf;
        resolved = true;
        break;
      }
      case AOTEffectKind::ColorMatrix: {
        auto* params = std::get_if<AOTColorMatrixParameters>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::Pointwise;
          action.pointwise.op = AOTChainOp::ColorMatrix;
          action.pointwise.colorMatrix = *params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::Luma: {
        auto* params = std::get_if<ChainISA::LumaData>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::Pointwise;
          action.pointwise.op = AOTChainOp::Luma;
          action.pointwise.luma = *params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::AlphaThreshold: {
        auto* params = std::get_if<ChainISA::AlphaThresholdData>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::Pointwise;
          action.pointwise.op = AOTChainOp::AlphaThreshold;
          action.pointwise.alphaThreshold = *params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::ColorSpaceXform: {
        auto* params = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::Pointwise;
          action.pointwise.op = AOTChainOp::ColorSpaceXform;
          action.pointwise.colorSpaceXform = *params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::GradientSource: {
        auto* params = std::get_if<AOTGradientParameters>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::Gradient;
          action.gradient = params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::RectCoverage: {
        auto* params = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::RectClip;
          action.rectClip = params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::RRectCoverage: {
        auto* params = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
        if (params != nullptr) {
          action.kind = Action::Kind::RRectClip;
          action.rrectClip = params;
          resolved = true;
        }
        break;
      }
      case AOTEffectKind::OpaqueKernel: {
        auto* params = std::get_if<AOTOpaqueKernelParameters>(&node->parameters);
        if (params == nullptr) {
          break;
        }
        action.kind = Action::Kind::Blur;
        action.blur = params;
        if (params->kernel != AOTOpaqueKernel::GaussianBlur1D) {
          hasProcessorBoundOpaqueKernel = true;
        }
        if (!node->inputs.empty()) {
          const auto* childNode = graph.nodeAt(node->inputs[0]);
          if (childNode != nullptr && childNode->kind == AOTEffectKind::TextureSource) {
            action.blurChild = std::get_if<AOTTextureParameters>(&childNode->parameters);
          }
        }
        resolved = true;
        break;
      }
      default:
        break;
    }
    if (resolved) {
      actions.push_back(action);
    }
    for (const auto& input : node->inputs) {
      self(graph, self, input);
    }
  };
  if (carriedIR->color.root().isValid()) {
    collect(carriedIR->color, collect, carriedIR->color.root());
  }
  if (hasProcessorBoundOpaqueKernel) {
    writeProcessorsByTree(info, vertexUniformData, fragmentUniformData);
    return;
  }
  for (const auto& term : carriedIR->coverage) {
    switch (term.kind) {
      case AOTCoverageTermKind::DeviceMask:
        if (term.mask.textureProxy != nullptr) {
          Action action = {};
          action.kind = Action::Kind::DeviceMask;
          action.leaf = &term.mask;
          actions.push_back(action);
        }
        break;
      case AOTCoverageTermKind::Analytic:
      case AOTCoverageTermKind::Subtree:
      case AOTCoverageTermKind::Glyph:
        if (term.root.isValid()) {
          collect(term.graph, collect, term.root);
        }
        break;
    }
  }
  for (const auto& action : actions) {
    paramWriter.setInstanceOrdinal(action.ordinal);
    switch (action.kind) {
      case Action::Kind::PlainLeaf:
        BindTextureSourceLeaf(*action.leaf, &paramWriter);
        break;
      case Action::Kind::TiledLeaf:
        BindTiledSourceLeaf(*action.leaf, &paramWriter);
        break;
      case Action::Kind::DeviceMask:
        BindDeviceMaskLeaf(*action.leaf, &paramWriter);
        break;
      case Action::Kind::RectClip:
        if (action.rectClip != nullptr) {
          BindRectClipNode(*action.rectClip, &paramWriter);
        }
        break;
      case Action::Kind::RRectClip:
        if (action.rrectClip != nullptr) {
          BindRRectClipNode(*action.rrectClip, &paramWriter);
        }
        break;
      case Action::Kind::Pointwise:
        // One chain slot record for every pointwise operator: the packed word (operator value and
        // selector bits) plus the operator's parameter registers.
        UploadPointwiseSlot(fragmentUniformData, action.pointwise);
        if (action.pointwise.op == AOTChainOp::ColorSpaceXform &&
            action.pointwise.colorSpaceXform.steps != nullptr) {
          // The color-space parameters are one program-level block; the "Chain" prefix is the
          // block the interpreter reads (shared with the chain and the YUV tail).
          ColorSpaceXformHelper("Chain").setData(fragmentUniformData,
                                                 action.pointwise.colorSpaceXform.steps.get());
        }
        break;
      case Action::Kind::Gradient:
        if (action.gradient != nullptr) {
          BindGradientNode(*action.gradient, &paramWriter);
        }
        break;
      case Action::Kind::Blur:
        if (action.blur != nullptr) {
          BindBlurKernelNode(*action.blur, action.blurChild, &paramWriter);
        }
        break;
    }
  }
  const auto processor = info.getXferProcessor();
  paramWriter.setInstanceOrdinal(0);
  if (ClassifyXferForMatch(processor) != XferKind::None) {
    // The blend mode off the IR; the dst texture's instance-time facts off the made processor --
    // the same layer the geometry facts read.
    BindPorterDuffXfer(static_cast<int>(carriedIR->blendMode), processor, &paramWriter);
  } else {
    processor->setData(vertexUniformData, fragmentUniformData);
  }
  if (bindYUVTail) {
    // The YUV family's tail instruction records come from the compiled program — the
    // shell-free value write the chain route applies to its own slots.
    auto* program = AOTChainBuilder::ResolveYUVProgram(&info);
    if (program == nullptr) {
      LOGE(
          "PrecompiledProgramWriter::writeProcessors() no compiled YUV tail program; the draw "
          "renders with unwritten tail uniforms");
      return;
    }
    BindYUVProgram(*program, fragmentUniformData);
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

bool PrecompiledProgramWriter::canWrite(const ProgramInfo& info) const {
  // Only the YUV family's tail needs a compiled program beyond the walk's own content; every
  // other family uploads the walk alone.
  return !bindYUVTail || AOTChainBuilder::ResolveYUVProgram(&info) != nullptr;
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
  const auto processor = info.getXferProcessor();
  const auto* chainIR = info.getCarriedIR();
  if (chainIR != nullptr && ClassifyXferForMatch(processor) != XferKind::None) {
    BindPorterDuffXfer(static_cast<int>(chainIR->blendMode), processor, &paramWriter);
  } else {
    processor->setData(vertexUniformData, fragmentUniformData);
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
#if DEBUG
  const char* unifDump = UnifDumpLabel();
#endif
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
#if DEBUG
    DebugDumpSamplerSlot(unifDump, textureBinding, textureView->getTexture().get(), false);
#endif
    renderPass->setTexture(textureBinding++, textureView->getTexture(), sampler);
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

}  // namespace tgfx
