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

#include "PrecompiledProgramCreator.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include "core/utils/Log.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramWriter.h"
#include "gpu/UniformData.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/ChainProgramEmit.h"
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/aot/chain/ChainSPIRVBody.h"
#include "gpu/aot/chain/ChainSPIRVToMSL.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/aot/plan/ChainBindingWriter.h"
#include "gpu/aot/plan/DrawPlan.h"
#include "gpu/aot/select/PrecompiledDiagnostics.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

namespace {

/// Whether the assembled route is active for a profile: the per-profile whitelist plus the two
/// forcing switches (see AssembledChainEnabledForProfile).
bool AssembledChainEnabled(const std::string& profileTag) {
  return AssembledChainEnabledForProfile(profileTag);
}

}  // namespace

// The assembled chain text spells the draw's expression, so a program's identity must carry it:
// the text serves exactly the shape it was planned for. The key folds the plan itself — the calls
// in instruction order and the epilogue facts — so a structural fact the text derives from cannot
// miss the way a hand-rolled shape digest could. A shape the assembled walk declines keeps the
// declined marker: its program is the variant's generic payload, distinct from every assembled
// program of the same variant.
static void AppendAssembledChainKey(Context* context, const ProgramInfo& info,
                                    const DrawPlan& resolved, BytesKey* key) {
  auto cache = ContextCacheAccess::Cache(context);
  if (!AssembledChainEnabled(cache->profileTag()) || resolved.sources.chain == nullptr) {
    return;
  }
  if (strcmp(Families()[resolved.variant.family].name, "Chain") != 0) {
    return;
  }
  const auto payloadFormat = SpecFormatForBackend(context->backend(), cache->payloadFormat());
  if (payloadFormat != ShaderCodeFormat::GLSL && payloadFormat != ShaderCodeFormat::SPIRV &&
      payloadFormat != ShaderCodeFormat::METALLIB && payloadFormat != ShaderCodeFormat::MSL) {
    return;
  }
  const auto* ir = info.getCarriedIR();
  if (ir == nullptr) {
    return;
  }
  // The assembled text carries only the draw's blend mode, so the program identity must separate
  // modes even when the rest of the shape agrees.
  key->write(static_cast<uint32_t>(info.getBlendMode()));
  const bool xpPass =
      XpNeedsAdvancedMath(info.getXferFacts().shaderBlend(), static_cast<int>(info.getBlendMode()));
  ChainProgramPlan plan = {};
  if (!PlanChainProgramFromGraph(*ir, xpPass, Families()[resolved.variant.family], resolved.variant,
                                 cache, &plan)) {
    key->write(0u);
    return;
  }
  key->write(1u);
  AppendChainPlanKey(plan, key);
}

// Which descriptor field carries the blob. Derived from the format rather than from the bundle's
// payload field so the two can never disagree: a format that names a container reads binaryData,
// a format that names driver source reads code.
static bool IsBinaryFormat(ShaderCodeFormat format) {
  return format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::METALLIB ||
         format == ShaderCodeFormat::DXBC;
}

// The coarse category a miss is counted under: every walk-side rejection the cache and its
// statistics treat alike collapses into NoMatchingRule, while the finer value stays in the
// record's matchFailure field. Folding is deliberate, not a lost distinction.
static AOTFailure ToFallbackReason(AOTFailure failure) {
  switch (failure) {
    case AOTFailure::UnsupportedOutputSwizzle:
      return AOTFailure::UnsupportedOutputSwizzle;
    case AOTFailure::NoMatchingRule:
      return AOTFailure::NoMatchingRule;
    case AOTFailure::UnsupportedTextureRead:
      return AOTFailure::UnsupportedTextureRead;
    case AOTFailure::GlGateRejected:
    case AOTFailure::QuadSubset:
    case AOTFailure::GradientOrNoiseBudget:
    case AOTFailure::CoverageShape:
    case AOTFailure::NoMatchingFamily:
    case AOTFailure::PermutationNotEnumerated:
      return AOTFailure::NoMatchingRule;
    case AOTFailure::None:
      return AOTFailure::Unspecified;
    default:
      return AOTFailure::Unspecified;
  }
  return AOTFailure::Unspecified;
}

// Whether a runtime vertex format feeds a shader input the declaration types as `declared`.
// Half-precision and normalized byte attributes are read as their float forms by the precompiled
// text, so those feed the matching float input; anything else must agree exactly.
static bool VertexFormatFeedsInput(VertexFormat runtime, UniformFormat declared) {
  switch (runtime) {
    case VertexFormat::Float:
    case VertexFormat::Half:
    case VertexFormat::UByteNormalized:
      return declared == UniformFormat::Float;
    case VertexFormat::Float2:
    case VertexFormat::Half2:
    case VertexFormat::UByte2Normalized:
      return declared == UniformFormat::Float2;
    case VertexFormat::Float3:
    case VertexFormat::Half3:
    case VertexFormat::UByte3Normalized:
      return declared == UniformFormat::Float3;
    case VertexFormat::Float4:
    case VertexFormat::Half4:
    case VertexFormat::UByte4Normalized:
      return declared == UniformFormat::Float4;
    case VertexFormat::Int:
      return declared == UniformFormat::Int;
    case VertexFormat::Int2:
      return declared == UniformFormat::Int2;
    case VertexFormat::Int3:
      return declared == UniformFormat::Int3;
    case VertexFormat::Int4:
      return declared == UniformFormat::Int4;
  }
  return false;
}

// The one vertex-interface check: the geometry's attribute must feed the declaration's input of
// the same position. A disagreement means the module's element list and the geometry's attribute
// list have drifted, so the draw is refunded rather than bound wrongly.
static bool DeclaredInputAcceptsAttribute(PrecompiledShaderCache* cache,
                                          const ProgramInfo* programInfo, const Variant& variant,
                                          const char* shaderName, const Attribute& runtime,
                                          const Layout::Attribute& declared, const char* side) {
  if (VertexFormatFeedsInput(runtime.format(), declared.format)) {
    return true;
  }
  cache->recordFailure(AOTFailure::VertexLayoutMismatch,
                       MakeFallbackRecord(cache, programInfo, &variant));
  LOGE(
      "PrecompiledProgramCreator: vertex interface mismatch for %s[variant=%u]: %s attribute %s "
      "arrives as format %d, the declaration reads it as %d",
      shaderName, variant.id(), side, runtime.name().c_str(), static_cast<int>(runtime.format()),
      static_cast<int>(declared.format));
  return false;
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgram(Context* context,
                                                                  ProgramInfo* programInfo) {
  auto cache = ContextCacheAccess::Cache(context);
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  if (!cache->isLoaded()) {
    // Say why nothing is loaded: a backend or build with no bundle is by design, a rejected bundle
    // is a defect that needs regenerating. Anything else (a standalone cache, for example) keeps
    // the generic reason.
    auto reason = AOTFailure::CacheNotLoaded;
    switch (cache->embeddedBundleState()) {
      case EmbeddedBundleState::NoBundleForBackend:
        reason = AOTFailure::NoBundleForBackend;
        break;
      case EmbeddedBundleState::Rejected:
        reason = AOTFailure::BundleRejected;
        break;
      default:
        break;
    }
    cache->recordArtifactMiss(reason, MakeFallbackRecord(cache, programInfo));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);

  AOTFailure matchFailure = AOTFailure::None;
  ShapeKey shapeKey = {};
  bool answeredFromCache = false;
  auto selection =
      SelectProgram(context, *programInfo, &matchFailure, &shapeKey, &answeredFromCache);
  if (!selection) {
    if (answeredFromCache) {
      // The negative table answered; the selection already counted the cached miss.
      return nullptr;
    }
    auto reason = ToFallbackReason(matchFailure);
    if (reason == AOTFailure::NoMatchingRule) {
      // A sampler whose proxy resolves no view yet (a lazy/deferred upload) drops its content
      // through the runtime stub program by design; classify it apart from real coverage gaps so
      // the production metrics stay honest. The record rides the IR, not the processor list.
      const auto* ir = programInfo->getCarriedIR();
      if (ir != nullptr) {
        for (const auto& proxy : ir->textures) {
          if (proxy != nullptr && proxy->getTextureView() == nullptr) {
            reason = AOTFailure::DeferredTexture;
            break;
          }
        }
      }
    }
    auto record = MakeFallbackRecord(cache, programInfo);
    record.matchFailure = static_cast<uint8_t>(matchFailure);
    cache->recordArtifactMiss(reason, record);
    if (reason != AOTFailure::DeferredTexture) {
      // A deferred texture may be instantiated later, which can make the same key serviceable:
      // only the definitive refusals enter the negative cache.
      cache->recordProgramMiss(shapeKey, reason);
    }
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);
  // The whole decision stays on the info before the cache lookup: the key reads it, and the upload
  // reads its sources instead of re-running the family rule, whether the program comes from the
  // cache or is created below.
  DrawPlan plan = {};
  plan.variant = selection->variant;
  plan.sources = selection->sources;
  plan.spec = selection->spec;
  plan.outputSwizzle = programInfo->getOutputSwizzle();
  programInfo->setPlan(plan);
  // The selection names the program: its variant selects the blob pair and its specialization
  // values tell two chain shapes of one variant apart (a rect clip's anti-alias flag, for one, is
  // not in the processor tree), so the generic form and every specialized form of one variant
  // are distinct programs under distinct keys.
  const auto& resolved = *programInfo->getPlan();
  BytesKey key = MakeProgramKey(context, *programInfo, resolved.variant.id(), resolved.spec);
  AppendAssembledChainKey(context, *programInfo, resolved, &key);
  if (auto cached = context->globalCache()->findProgram(key)) {
    return cached;
  }
  auto program = CreateProgramForVariant(context, programInfo, resolved.variant);
  if (program != nullptr) {
    // A specialization that failed to build still returns the generic form (see the fallback in
    // CreateProgramForVariant); caching it under this key keeps the failing compile from being
    // retried.
    context->globalCache()->addProgram(key, program);
  }
  return program;
}

namespace {

// The one fallback: an assembled program whose module or pipeline failed to build must not drop
// the draw silently — with the interpreter retired there is no generic precompiled form to fall
// back to, so the null program sends the draw down the runtime path and the failure keeps its
// record.
void ReportAssembledBuildFailure(Context* context, const ProgramInfo* programInfo,
                                 const Variant& variant, const char* stage) {
  auto cache = ContextCacheAccess::Cache(context);
  LOGE(
      "PrecompiledProgramCreator: the assembled %s of %s[variant=%u] failed to build; the draw "
      "keeps its runtime path",
      stage, Families()[variant.family].name, variant.id());
  cache->recordFailure(AOTFailure::PipelineCreationFailed,
                       MakeFallbackRecord(cache, programInfo, &variant));
}

}  // namespace

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgramForVariant(
    Context* context, const ProgramInfo* programInfo, const Variant& variant) {
  return CreateProgramForVariantInternal(context, programInfo, variant, true);
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgramForVariantInternal(
    Context* context, const ProgramInfo* programInfo, const Variant& variant, bool allowComposed) {
  auto cache = ContextCacheAccess::Cache(context);
  // The callers hold their own resolved variant in hand (CreateProgram just ran the walk; the AOT
  // route derives its variant from the compiled program), so this entry starts past the match.
  const char* shaderName = Families()[variant.family].name;
  const uint32_t variantId = variant.id();

  auto vertBlob = cache->findVertex(variantId);
  if (vertBlob == nullptr) {
    cache->recordArtifactMiss(AOTFailure::VertexArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledShaderMiss: vert blob not found for %s[variant=%u]", shaderName, variantId);
    return nullptr;
  }
  auto fragBlob = cache->findFragment(variantId);
  if (fragBlob == nullptr) {
    cache->recordArtifactMiss(AOTFailure::FragmentArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledShaderMiss: frag blob not found for %s[variant=%u]", shaderName, variantId);
    return nullptr;
  }
  cache->recordArtifactHit();

  // The interface of this program, derived from its declaration: the artifact supplies only the
  // compiled bytes. The layout is the single numbering source the assembled text was generated
  // from and the writers upload through, so nothing here reads the bundle's reflection.
  const Layout layout = DeriveLayout(variant);
  auto format = SpecFormatForBackend(context->backend(), cache->payloadFormat());
  const bool binaryPayload = IsBinaryFormat(format);

  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = format;
  vertexDesc.stage = ShaderStage::Vertex;
  if (binaryPayload) {
    vertexDesc.binaryData = vertBlob->data;
  } else {
    vertexDesc.code = std::string(vertBlob->data.begin(), vertBlob->data.end());
  }
  if (layout.vertexBlockSize > 0) {
    // The stage's uniform block bindings are constants baked into every offline template (see
    // UniformData.h); a stage without uniforms stays slot-less so the pipeline-time two-way
    // layout check sees neither side.
    vertexDesc.uniformSlots = {
        {std::string(VertexUniformBlockName), static_cast<unsigned>(VERTEX_UBO_BINDING_POINT)}};
  }

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = format;
  fragmentDesc.stage = ShaderStage::Fragment;
  if (binaryPayload) {
    fragmentDesc.binaryData = fragBlob->data;
  } else {
    fragmentDesc.code = std::string(fragBlob->data.begin(), fragBlob->data.end());
  }
  // The assembled executor: the graph walk — the emit declares exactly the members the
  // expression reads, the body is the pack's per-op entries, and the writer walks the emit's own
  // tables. A shape the walk declines has no assembled program; with the interpreter retired the
  // draw then keeps its runtime path (the caller treats the null program as a miss).
  bool assembledAttempt = false;
  ChainBindings chainBindings = {};
  const bool assembledVulkan = format == ShaderCodeFormat::SPIRV;
  const bool assembledMetal = format == ShaderCodeFormat::METALLIB;
  if (allowComposed && AssembledChainEnabled(cache->profileTag()) &&
      (format == ShaderCodeFormat::GLSL || assembledVulkan || assembledMetal) &&
      strcmp(Families()[variant.family].name, "Chain") == 0) {
    const auto* assembledSources = programInfo->getSources();
    const auto* assembledIR = programInfo->getCarriedIR();
    if (assembledSources != nullptr && assembledSources->chain != nullptr &&
        assembledIR != nullptr) {
      const Family& chainFamily = Families()[variant.family];
      const bool xpPass = XpNeedsAdvancedMath(programInfo->getXferFacts().shaderBlend(),
                                              static_cast<int>(programInfo->getBlendMode()));
      ChainProgramPlan plan = {};
      const bool planOk =
          PlanChainProgramFromGraph(*assembledIR, xpPass, chainFamily, variant, cache, &plan);
      if (!planOk) {
        // The assembled walk is the chain's one serving path: a shape it declines has no
        // precompiled program (the interpreter is retired), so the draw keeps its runtime path
        // instead of serving a payload nothing could upload.
        LOGE("AssembledChain: the plan declined %s[variant=%u]; the draw keeps its runtime path",
             shaderName, variantId);
        cache->recordFailure(AOTFailure::NoMatchingRule,
                             MakeFallbackRecord(cache, programInfo, &variant));
        return nullptr;
      }
      if ((assembledVulkan || assembledMetal) && plan.geometry != 0) {
        // The linking route has not learned the per-pixel geometry coverage formulas yet; the
        // GLSL printer is the only assembled path that spells them.
        LOGE(
            "AssembledChain: the linking route declines %s[variant=%u] geometry %u; the draw "
            "keeps its runtime path",
            shaderName, variantId, plan.geometry);
        cache->recordFailure(AOTFailure::NoMatchingRule,
                             MakeFallbackRecord(cache, programInfo, &variant));
        return nullptr;
      }
      {
        if (assembledVulkan || assembledMetal) {
          // The linking route: the precompiled frame's entry calls a body emitted here, linked
          // with the op modules the plan reaches. Shapes outside the linkable slice keep their
          // runtime path.
          const int hasXp = XferDimValue(chainFamily, variant);
          if (ChainSPIRVBodySupported(plan.usedOps, layout, hasXp)) {
            std::vector<uint32_t> body = {};
            std::string emitError;
            if (EmitChainBodySPIRV(plan, layout, &body, &emitError)) {
              std::vector<std::vector<uint32_t>> modules = {};
              bool modulesOk = true;
              const auto* frameEntry = cache->findComposed(FragmentPackSPIRVFrameBase | variantId);
              if (frameEntry == nullptr) {
                modulesOk = false;
                LOGE("AssembledChain: no SPIR-V frame for variant %u", variantId);
              } else {
                std::vector<uint32_t> frameWords(frameEntry->data.size() / 4);
                std::memcpy(frameWords.data(), frameEntry->data.data(),
                            frameWords.size() * sizeof(uint32_t));
                modules.push_back(std::move(frameWords));
              }
              if (modulesOk) {
                modules.push_back(std::move(body));
                std::vector<uint32_t> moduleOps = {};
                for (int usedOp : plan.usedOps) {
                  moduleOps.push_back(static_cast<uint32_t>(usedOp));
                }
                bool wantsLeafFetch = false;
                for (uint32_t op : moduleOps) {
                  if (op == ChainISA::Texture || op == ChainISA::TexModulate) {
                    wantsLeafFetch = true;
                  }
                }
                if (wantsLeafFetch) {
                  moduleOps.push_back(FragmentPackSPIRVLeafFetch & ~FragmentPackSPIRVOpBase);
                }
                if (hasXp == 1) {
                  moduleOps.push_back(FragmentPackSPIRVPortDuff & ~FragmentPackSPIRVOpBase);
                }
                for (uint32_t op : moduleOps) {
                  if (op == ChainISA::InputOpaque || op == ChainISA::MulAlpha) {
                    continue;  // the inline expressions need no module
                  }
                  const auto* opEntry =
                      cache->findComposed(FragmentPackSPIRVOpBase | static_cast<uint32_t>(op));
                  if (opEntry == nullptr) {
                    modulesOk = false;
                    LOGE("AssembledChain: no SPIR-V op module for %d", op);
                    break;
                  }
                  std::vector<uint32_t> opWords(opEntry->data.size() / 4);
                  std::memcpy(opWords.data(), opEntry->data.data(),
                              opWords.size() * sizeof(uint32_t));
                  modules.push_back(std::move(opWords));
                }
                std::vector<uint32_t> linked = {};
                std::string linkError;
                if (modulesOk && LinkChainSPIRV(modules, &linked, &linkError)) {
                  if (assembledMetal) {
                    // Metal has no binary linker: the linked module is translated to MSL and the
                    // driver compiles it at pipeline creation (the module cache keys it like any
                    // other precompiled payload).
                    std::string msl = {};
                    std::string mslError = {};
                    if (TranslateChainSPIRVToMSL(linked, &msl, &mslError)) {
                      assembledAttempt = true;
                      chainBindings = plan.bindings;
                      fragmentDesc.code = std::move(msl);
                      fragmentDesc.format = ShaderCodeFormat::MSL;
                      LOGI("AssembledChain: translated Metal entry for %s[variant=%u]", shaderName,
                           variantId);
                    } else {
                      LOGE("AssembledChain: Metal translation declined for %s[variant=%u]: %s",
                           shaderName, variantId, mslError.c_str());
                    }
                  } else {
                    assembledAttempt = true;
                    chainBindings = plan.bindings;
                    const auto* linkedBytes = reinterpret_cast<const uint8_t*>(linked.data());
                    fragmentDesc.binaryData.assign(linkedBytes,
                                                   linkedBytes + linked.size() * sizeof(uint32_t));
                    fragmentDesc.code.clear();
                    LOGI("AssembledChain: linked Vulkan entry for %s[variant=%u] (%zu modules)",
                         shaderName, variantId, modules.size());
                  }
                } else if (modulesOk) {
                  LOGE("AssembledChain: Vulkan link declined for %s[variant=%u]: %s", shaderName,
                       variantId, linkError.c_str());
                }
              }
            } else {
              LOGE("AssembledChain: Vulkan body declined for %s[variant=%u]: %s", shaderName,
                   variantId, emitError.c_str());
            }
          }
        } else if (EmitChainProgramGLSL(*assembledIR, xpPass, chainFamily, variant, cache,
                                        &fragmentDesc.code, &chainBindings,
                                        cache->profileTag() == "opengles",
                                        static_cast<int>(programInfo->getBlendMode()))) {
          assembledAttempt = true;
          LOGI("AssembledChain: assembled entry for %s[variant=%u] (%zu bytes, %zu blocks)",
               shaderName, variantId, fragmentDesc.code.size(), chainBindings.fragment.size());
          // The vertex stage follows the same contract: the stored text's interface (attributes,
          // vertex block, geometry hook) stays, and the varying set bends to the expression's
          // leaves and resource reads.
          std::string assembledVs;
          if (AssembleChainVertexGLSL(chainBindings,
                                      std::string(vertBlob->data.begin(), vertBlob->data.end()),
                                      &assembledVs)) {
            vertexDesc.code = std::move(assembledVs);
          }
        }
      }
    }
  }
  // The format's injector decides where the values land: the driver-source formats rewrite the
  // generated TGFX_SPEC_<id> declarations (the driver's compiler then folds every accessor read
  // and the unrolled executor's guards), the container formats append the pipeline's constant
  // table. A format without a registered injector serves the generic form.
  std::vector<std::pair<std::string, double>> specConstants = {};
  // The assembled text declares its own fragment block; every other route keeps the variant's
  // derived one.
  const uint32_t fragmentBlockSize =
      assembledAttempt ? chainBindings.fragmentBlockSize : layout.fragmentBlockSize;
  if (fragmentBlockSize > 0) {
    fragmentDesc.uniformSlots = {
        {std::string(FragmentUniformBlockName), static_cast<unsigned>(FRAGMENT_UBO_BINDING_POINT)}};
  }

  auto* shaderCache = cache->shaderCache();
  auto vertexShader = shaderCache->findOrCreateModule(vertexDesc);
  if (vertexShader == nullptr) {
    cache->recordFailure(AOTFailure::VertexModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledProgramCreator: Failed to create vertex shader module for %s[variant=%u]",
         shaderName, variantId);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  auto fragmentShader = shaderCache->findOrCreateModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    if (assembledAttempt) {
      ReportAssembledBuildFailure(context, programInfo, variant, "fragment module");
      return nullptr;
    }
    cache->recordFailure(AOTFailure::FragmentModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledProgramCreator: Failed to create fragment shader module for %s[variant=%u]",
         shaderName, variantId);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::FragmentModuleCreated);

  RenderPipelineDescriptor descriptor = {};
  // The vertex interface comes from the declaration: each element's location is the one the
  // assembled text declares, and the element kind splits the two buffer layouts. A geometry may
  // carry attributes the program does not read (a per-vertex color a kernel takes from a uniform,
  // a slot its variant filters out); those keep their buffer entry and park on spare locations
  // past the declared range — that is what a rendered pipeline needs, not a guess about which
  // numbering convention a template used.
  const auto& geometryAttributes = programInfo->getVertexAttributes();
  const auto& instanceAttributes = programInfo->getInstanceAttributes();
  std::vector<const Layout::Attribute*> declaredVertex = {};
  std::vector<const Layout::Attribute*> declaredInstance = {};
  uint32_t spareLocation = 0;
  for (const auto& attribute : layout.attributes) {
    spareLocation = std::max(spareLocation, static_cast<uint32_t>(attribute.location) + 1);
    if (attribute.instance) {
      declaredInstance.push_back(&attribute);
    } else {
      declaredVertex.push_back(&attribute);
    }
  }
  if (declaredVertex.size() > geometryAttributes.size() ||
      declaredInstance.size() > instanceAttributes.size()) {
    cache->recordFailure(AOTFailure::VertexLayoutMismatch,
                         MakeFallbackRecord(cache, programInfo, &variant));
    LOGE(
        "PrecompiledProgramCreator: vertex interface mismatch for %s[variant=%u]: the declaration "
        "reads %zu vertex and %zu instance attributes, the geometry provides %zu and %zu",
        shaderName, variantId, declaredVertex.size(), declaredInstance.size(),
        geometryAttributes.size(), instanceAttributes.size());
    return nullptr;
  }
  VertexBufferLayout vertexLayout(geometryAttributes);
  for (size_t index = 0; index < geometryAttributes.size(); ++index) {
    const bool declared = index < declaredVertex.size();
    if (declared && !DeclaredInputAcceptsAttribute(cache, programInfo, variant, shaderName,
                                                   geometryAttributes[index],
                                                   *declaredVertex[index], "vertex")) {
      return nullptr;
    }
    vertexLayout.shaderLocations.push_back(declared ? declaredVertex[index]->location
                                                    : spareLocation++);
  }
  descriptor.vertex.bufferLayouts = {vertexLayout};
  if (!instanceAttributes.empty()) {
    VertexBufferLayout instanceLayout(instanceAttributes, VertexStepMode::Instance);
    for (size_t index = 0; index < instanceAttributes.size(); ++index) {
      const bool declared = index < declaredInstance.size();
      if (declared && !DeclaredInputAcceptsAttribute(cache, programInfo, variant, shaderName,
                                                     instanceAttributes[index],
                                                     *declaredInstance[index], "instance")) {
        return nullptr;
      }
      instanceLayout.shaderLocations.push_back(declared ? declaredInstance[index]->location
                                                        : spareLocation++);
    }
    descriptor.vertex.bufferLayouts.push_back(instanceLayout);
  }
  // Precompiled GLSL declares attributes with layout(location=N) matching the flattened order of
  // the runtime attribute list. Names in the template differ from the GeometryProcessor's runtime
  // names, so name-based GL binding would silently drop attributes.
  descriptor.vertex.bindAttributesByLocation = true;
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.constants = std::move(specConstants);
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());

  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  if (layout.vertexBlockSize > 0) {
    vertexUniformData = std::unique_ptr<UniformData>(new UniformData(layout.vertexBlockSize));
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (fragmentBlockSize > 0) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(fragmentBlockSize));
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  // The assembled text declares its own samplers in its own order, so the emit's table is the
  // binding list; every other route declares the variant's.
  if (assembledAttempt && !assembledVulkan) {
    // The GL text declares its samplers in the plan's order, so the emit's table is the binding
    // list.
    for (size_t index = 0; index < chainBindings.samplers.size(); ++index) {
      descriptor.layout.textureSamplers.emplace_back(chainBindings.samplers[index].name,
                                                     static_cast<unsigned>(index));
    }
  } else if (assembledAttempt && assembledVulkan) {
    // The Vulkan modules declare every leaf sampler (the fetch's index dispatch reads them
    // statically) and the mask, so the descriptor carries the plan's used samplers first — at
    // their real bindings, in slot order — and appends the variant's remaining textures for
    // coverage; the writer binds only the first block, through the program's slot map.
    unsigned leafCount = 0;
    for (const auto& texture : layout.textures) {
      if (texture.name.rfind("TextureSampler_", 0) == 0) {
        ++leafCount;
      }
    }
    for (const auto& sampler : chainBindings.samplers) {
      unsigned binding = static_cast<unsigned>(sampler.index);
      if (sampler.source != ChainSamplerSource::Leaf) {
        // The mask and the destination sampler sit where the variant's layout puts them.
        binding = leafCount;
        for (const auto& texture : layout.textures) {
          if (texture.name == sampler.name) {
            binding = texture.binding;
          }
        }
      }
      descriptor.layout.textureSamplers.emplace_back(sampler.name, binding);
    }
    for (const auto& texture : layout.textures) {
      const bool listed = std::any_of(
          descriptor.layout.textureSamplers.begin(), descriptor.layout.textureSamplers.end(),
          [&](const BindingEntry& entry) { return entry.name == texture.name; });
      if (!listed) {
        descriptor.layout.textureSamplers.emplace_back(texture.name, texture.binding);
      }
    }
  } else {
    for (const auto& texture : layout.textures) {
      descriptor.layout.textureSamplers.emplace_back(texture.name, texture.binding);
    }
  }

  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();

  auto pipeline = shaderCache->findOrCreatePipeline(descriptor);
  if (pipeline == nullptr) {
    if (assembledAttempt) {
      ReportAssembledBuildFailure(context, programInfo, variant, "pipeline");
      return nullptr;
    }
    // The same shader variant (identical vert/frag artifact) can create successfully for
    // some draws and fail for others, so the mismatch lives in the runtime-derived pipeline
    // state rather than the compiled modules; dump that state for a failing-versus-succeeding
    // diff.
    LogPipelineCreationFailure(descriptor, shaderName, variantId);

    cache->recordFailure(AOTFailure::PipelineCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &variant));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PipelineCreated,
                        MakeHitRecord(cache, programInfo, variant));

  auto program = std::make_shared<Program>(
      std::move(pipeline), std::move(vertexUniformData), std::move(fragmentUniformData),
      assembledAttempt
          ? std::unique_ptr<ProgramWriter>(new ChainBindingWriter(variant, chainBindings))
          : std::unique_ptr<ProgramWriter>(new ProgramWriter(variant)));
  program->setShaderName(shaderName);
  std::vector<unsigned> textureSlotBindings = {};
  for (const auto& entry : descriptor.layout.textureSamplers) {
    textureSlotBindings.push_back(entry.binding);
  }
  program->setTextureSlotBindings(std::move(textureSlotBindings));
  program->setExpectedTextureCount(
      assembledAttempt ? static_cast<uint32_t>(chainBindings.samplers.size())
                       : static_cast<uint32_t>(descriptor.layout.textureSamplers.size()));
  return program;
}

}  // namespace tgfx
