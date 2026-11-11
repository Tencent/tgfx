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
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
#include "core/utils/Log.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramSignature.h"
#include "gpu/ProgramWriter.h"
#include "gpu/UniformData.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/aot/select/PermutationMatch.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/ProgramWriterFactory.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Which descriptor field carries the blob. Derived from the format rather than from the bundle's
// payload field so the two can never disagree: a format that names a container reads binaryData,
// a format that names driver source reads code.
static bool IsBinaryFormat(ShaderCodeFormat format) {
  return format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::METALLIB ||
         format == ShaderCodeFormat::DXBC;
}

static void AppendFragmentProcessor(std::stringstream& stream, const FragmentProcessor* processor) {
  stream << processor->name();
  if (processor->numChildProcessors() == 0) {
    return;
  }
  stream << "(";
  for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, processor->childProcessor(i));
  }
  stream << ")";
}

std::string BuildEffectSignature(const ProgramInfo* programInfo) {
  std::stringstream stream;
  stream << "GP=" << programInfo->getGeometryProcessor()->name() << ";ColorFP=[";
  for (size_t i = 0; i < programInfo->numColorFragmentProcessors(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, programInfo->getFragmentProcessor(i));
  }
  stream << "];CoverageFP=[";
  for (size_t i = programInfo->numColorFragmentProcessors();
       i < programInfo->numFragmentProcessors(); ++i) {
    if (i > programInfo->numColorFragmentProcessors()) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, programInfo->getFragmentProcessor(i));
  }
  stream << "];XP=" << programInfo->getXferProcessor()->name();
  return stream.str();
}

static void AppendStencilSignature(std::stringstream& stream, const char* name,
                                   const StencilDescriptor& stencil) {
  stream << ";" << name << "Compare=" << static_cast<uint32_t>(stencil.compare) << ";" << name
         << "Fail=" << static_cast<uint32_t>(stencil.failOp) << ";" << name
         << "DepthFail=" << static_cast<uint32_t>(stencil.depthFailOp) << ";" << name
         << "Pass=" << static_cast<uint32_t>(stencil.passOp);
}

static std::string BuildPipelineSignature(const ProgramInfo* programInfo) {
  std::stringstream stream;
  const auto colorAttachment = programInfo->getPipelineColorAttachment();
  const auto& depthStencil = programInfo->getDepthStencil();
  stream << BuildEffectSignature(programInfo)
         << ";Swizzle=" << programInfo->getOutputSwizzle().c_str()
         << ";Format=" << static_cast<uint32_t>(colorAttachment.format)
         << ";BlendEnable=" << static_cast<uint32_t>(colorAttachment.blendEnable)
         << ";SrcColorBlend=" << static_cast<uint32_t>(colorAttachment.srcColorBlendFactor)
         << ";DstColorBlend=" << static_cast<uint32_t>(colorAttachment.dstColorBlendFactor)
         << ";ColorBlendOp=" << static_cast<uint32_t>(colorAttachment.colorBlendOp)
         << ";SrcAlphaBlend=" << static_cast<uint32_t>(colorAttachment.srcAlphaBlendFactor)
         << ";DstAlphaBlend=" << static_cast<uint32_t>(colorAttachment.dstAlphaBlendFactor)
         << ";AlphaBlendOp=" << static_cast<uint32_t>(colorAttachment.alphaBlendOp)
         << ";Samples=" << programInfo->getSampleCount()
         << ";Cull=" << static_cast<uint32_t>(programInfo->getCullMode())
         << ";ColorWriteMask=" << colorAttachment.colorWriteMask
         << ";DepthStencilFormat=" << static_cast<uint32_t>(depthStencil.format)
         << ";DepthCompare=" << static_cast<uint32_t>(depthStencil.depthCompare)
         << ";DepthWrite=" << static_cast<uint32_t>(depthStencil.depthWriteEnabled)
         << ";StencilReadMask=" << depthStencil.stencilReadMask
         << ";StencilWriteMask=" << depthStencil.stencilWriteMask;
  AppendStencilSignature(stream, "StencilFront", depthStencil.stencilFront);
  AppendStencilSignature(stream, "StencilBack", depthStencil.stencilBack);
  return stream.str();
}

static PrecompiledFallbackRecord MakeFallbackRecord(
    const PrecompiledShaderCache* cache, const ProgramInfo* programInfo,
    const PermutationMatchResult* matchResult = nullptr) {
  PrecompiledFallbackRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  if (matchResult != nullptr) {
    record.shaderName = matchResult->shaderName;
    record.vertPermutationIndex = matchResult->variant.vertPermutationIndex;
    record.fragPermutationIndex = matchResult->variant.fragPermutationIndex;
  }
  return record;
}

static PrecompiledHitRecord MakeHitRecord(const PrecompiledShaderCache* cache,
                                          const ProgramInfo* programInfo,
                                          const PermutationMatchResult& matchResult) {
  PrecompiledHitRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  record.shaderName = matchResult.shaderName;
  record.vertPermutationIndex = matchResult.variant.vertPermutationIndex;
  record.fragPermutationIndex = matchResult.variant.fragPermutationIndex;
  return record;
}

static PrecompiledFallbackReason ToFallbackReason(PermutationMatchFailure failure) {
  switch (failure) {
    case PermutationMatchFailure::UnsupportedOutputSwizzle:
      return PrecompiledFallbackReason::UnsupportedOutputSwizzle;
    case PermutationMatchFailure::NoMatchingRule:
      return PrecompiledFallbackReason::NoMatchingRule;
    case PermutationMatchFailure::UnsupportedTextureRead:
      return PrecompiledFallbackReason::UnsupportedTextureRead;
    case PermutationMatchFailure::GlGateRejected:
    case PermutationMatchFailure::QuadSubset:
    case PermutationMatchFailure::GradientOrNoiseBudget:
    case PermutationMatchFailure::CoverageShape:
    case PermutationMatchFailure::NoMatchingFamily:
    case PermutationMatchFailure::PermutationNotEnumerated:
      return PrecompiledFallbackReason::NoMatchingRule;
    case PermutationMatchFailure::None:
      return PrecompiledFallbackReason::Unspecified;
    default:
      return PrecompiledFallbackReason::Unspecified;
  }
  return PrecompiledFallbackReason::Unspecified;
}

// True when the reflected vertex input at the given location (if the shader reads that slot)
// carries a format the runtime attribute can serve: half-precision and normalized byte
// attributes read as their float forms in the precompiled GLSL, everything else must match
// exactly. A slot with no reflected input (an attribute parked on a spare location) passes.
static bool VertexInputFormatMatches(const std::vector<VertexInput>& inputs, uint32_t location,
                                     VertexFormat runtimeFormat, const std::string& shaderName,
                                     PrecompiledShaderCache* cache, const ProgramInfo* programInfo,
                                     const PermutationMatchResult* matchResult) {
  const VertexInput* input = nullptr;
  for (const auto& candidate : inputs) {
    if (candidate.location == location) {
      input = &candidate;
      break;
    }
  }
  if (input == nullptr) {
    return true;
  }
  UniformFormat reflected = input->format;
  bool agrees = false;
  switch (runtimeFormat) {
    case VertexFormat::Float:
    case VertexFormat::Half:
    case VertexFormat::UByteNormalized:
      agrees = reflected == UniformFormat::Float;
      break;
    case VertexFormat::Float2:
    case VertexFormat::Half2:
    case VertexFormat::UByte2Normalized:
      agrees = reflected == UniformFormat::Float2;
      break;
    case VertexFormat::Float3:
    case VertexFormat::Half3:
    case VertexFormat::UByte3Normalized:
      agrees = reflected == UniformFormat::Float3;
      break;
    case VertexFormat::Float4:
    case VertexFormat::Half4:
    case VertexFormat::UByte4Normalized:
      agrees = reflected == UniformFormat::Float4;
      break;
    case VertexFormat::Int:
      agrees = reflected == UniformFormat::Int;
      break;
    case VertexFormat::Int2:
      agrees = reflected == UniformFormat::Int2;
      break;
    case VertexFormat::Int3:
      agrees = reflected == UniformFormat::Int3;
      break;
    case VertexFormat::Int4:
      agrees = reflected == UniformFormat::Int4;
      break;
  }
  if (agrees) {
    return true;
  }
  cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
                       MakeFallbackRecord(cache, programInfo, matchResult));
  LOGE(
      "PrecompiledProgramCreator: vertex format mismatch for %s[vert=%u]: location %u "
      "reflects format %d but the runtime attribute provides %d",
      shaderName.c_str(), matchResult->variant.vertPermutationIndex, location,
      static_cast<int>(reflected), static_cast<int>(runtimeFormat));
  return false;
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgram(Context* context,
                                                                  const ProgramInfo* programInfo) {
  auto cache = ContextCacheAccess::Cache(context);
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  if (!cache->isLoaded()) {
    // Say why nothing is loaded: a backend or build with no bundle is by design, a rejected bundle
    // is a defect that needs regenerating. Anything else (a standalone cache, for example) keeps
    // the generic reason.
    auto reason = PrecompiledFallbackReason::CacheNotLoaded;
    switch (cache->embeddedBundleState()) {
      case EmbeddedBundleState::NoBundleForBackend:
        reason = PrecompiledFallbackReason::NoBundleForBackend;
        break;
      case EmbeddedBundleState::Rejected:
        reason = PrecompiledFallbackReason::BundleRejected;
        break;
      default:
        break;
    }
    cache->recordArtifactMiss(reason, MakeFallbackRecord(cache, programInfo));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);

  const auto& programKey = programInfo->programKey();
  PrecompiledFallbackReason cachedReason = PrecompiledFallbackReason::Unspecified;
  if (cache->findProgramMiss(programKey, &cachedReason)) {
    // The first draw with this key walked the rules and was refused; every later draw carrying the
    // same key gets the same answer, so skip the walk (and the record formatting that goes with
    // it). The cache drops the entry whenever the bundle changes.
    cache->recordCachedMiss(cachedReason);
    return nullptr;
  }

  PermutationMatchFailure matchFailure = PermutationMatchFailure::None;
  auto selection = SelectProgram(context, *programInfo, SelectionScope::AnyFamily, &matchFailure);
  if (!selection) {
    auto reason = ToFallbackReason(matchFailure);
    if (reason == PrecompiledFallbackReason::NoMatchingRule) {
      // A texture FP whose view is not instantiated yet (a lazy/deferred proxy) drops its
      // content through the runtime stub program by design; classify it apart from real
      // coverage gaps so the production metrics stay honest.
      for (size_t i = 0; i < programInfo->numFragmentProcessors(); ++i) {
        auto fp = programInfo->getFragmentProcessor(i);
        bool textureFP = IsProcessor<TextureEffect>(fp) || IsProcessor<TiledTextureEffect>(fp) ||
                         IsProcessor<DeviceSpaceTextureEffect>(fp);
        if (textureFP && fp->numTextureSamplers() == 0) {
          reason = PrecompiledFallbackReason::DeferredTexture;
          break;
        }
      }
    }
    auto record = MakeFallbackRecord(cache, programInfo);
    record.matchFailure = static_cast<uint8_t>(matchFailure);
    // Offline decomposition-coverage audit: for structural misses, classify (during diagnostic
    // runs only) whether each effect axis could be reduced onto the existing AOT kernel basis. This
    // is pure static analysis — no rendering, no state change — gated so the production hot path
    // pays nothing.
    if (reason == PrecompiledFallbackReason::NoMatchingRule &&
        cache->diagnosticRecordingEnabled()) {
      record.decomposeAnalysis = AOTEffectDecomposer::Analyze(programInfo);
    }
    cache->recordArtifactMiss(reason, record);
    if (reason != PrecompiledFallbackReason::DeferredTexture) {
      // A deferred texture may be instantiated later, which can make the same key serviceable:
      // only the definitive refusals enter the negative cache.
      cache->recordProgramMiss(programKey, reason);
    }
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);
  // The selection names the program: its variant selects the blob pair and its specialization
  // values tell two chain shapes of one variant apart (a rect clip's anti-alias flag, for one, is
  // not in the processor tree), so the generic form and every specialized form of one variant
  // are distinct programs under distinct keys.
  PermutationMatchResult matchResult = {};
  matchResult.shaderName = Families()[selection->variant.family].name;
  matchResult.variant.vertPermutationIndex = selection->variant.ordinal;
  matchResult.variant.fragPermutationIndex = selection->variant.ordinal;
  const BytesKey key =
      MakeProgramKey(context, *programInfo, selection->variant.id(), selection->spec);
  if (auto cached = context->globalCache()->findProgram(key)) {
    return cached;
  }
  auto program = CreateProgramFromMatch(context, programInfo, matchResult,
                                        selection->spec.empty() ? nullptr : &selection->spec);
  if (program != nullptr) {
    // A specialization that failed to build still returns the generic form (see the fallback in
    // CreateProgramFromMatch); caching it under this key keeps the failing compile from being
    // retried.
    context->globalCache()->addProgram(key, program);
  }
  return program;
}

namespace {

// The one fallback: a specialization that failed to build must not drop the draw, and the generic
// text of the same variant always built, so re-enter without values (this recursion carries no
// spec and cannot loop). The caller caches the returned program under the specialization-bearing
// key, so this shape never retries the failing compile.
std::shared_ptr<Program> FallbackToGeneric(Context* context, const ProgramInfo* programInfo,
                                           const PermutationMatchResult& match, const char* stage) {
  LOGE(
      "PrecompiledProgramCreator: the specialized %s of %s[frag=%u] failed to build; falling "
      "back to the generic form",
      stage, match.shaderName.c_str(), match.variant.fragPermutationIndex);
  return PrecompiledProgramCreator::CreateProgramFromMatch(context, programInfo, match, nullptr);
}

}  // namespace

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgramFromMatch(
    Context* context, const ProgramInfo* programInfo, const PermutationMatchResult& match,
    const SpecValues* spec) {
  auto cache = ContextCacheAccess::Cache(context);
  // The callers hold their own match in hand (CreateProgram just ran the walk; the AOT route
  // derives its variant from the compiled program), so this entry starts past the match.
  const PermutationMatchResult* matchResult = &match;

  const uint16_t familyIndex = FamilyIndexOf(matchResult->shaderName.c_str());
  if (familyIndex == 0xFFFF) {
    LOGE("PrecompiledShaderMiss: unknown family %s", matchResult->shaderName.c_str());
    return nullptr;
  }
  const uint32_t vertId = VariantIdOf(familyIndex, matchResult->variant.vertPermutationIndex);
  const uint32_t fragId = VariantIdOf(familyIndex, matchResult->variant.fragPermutationIndex);

  auto vertBlob = cache->findVertex(vertId);
  if (vertBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: vert blob not found for %s[vert=%u]",
         matchResult->shaderName.c_str(), matchResult->variant.vertPermutationIndex);
    return nullptr;
  }
  auto fragBlob = cache->findFragment(fragId);
  if (fragBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::FragmentArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: frag blob not found for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->variant.fragPermutationIndex);
    return nullptr;
  }
  cache->recordArtifactHit();

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
  if (!vertBlob->uniforms.empty()) {
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
  // The format's injector decides where the values land: the driver-source formats rewrite the
  // generated TGFX_SPEC_<id> declarations (the driver's compiler then folds every accessor read
  // and the unrolled executor's guards), the container formats append the pipeline's constant
  // table. A format without a registered injector serves the generic form.
  bool specInjected = false;
  std::vector<std::pair<std::string, double>> specConstants = {};
  if (spec != nullptr && !spec->empty()) {
    if (const auto* injector = FindSpecInjector(format)) {
      SpecInjection injection = {};
      if (injector->Apply(fragmentDesc.code, fragBlob->specFields, *spec, &injection)) {
        if (!injection.code.empty()) {
          fragmentDesc.code = std::move(injection.code);
        }
        specConstants = std::move(injection.constants);
        specInjected = true;
      } else {
        LOGE(
            "PrecompiledProgramCreator: specialization injection found no TGFX_SPEC declarations "
            "for %s[frag=%u]; serving the generic form",
            matchResult->shaderName.c_str(), matchResult->variant.fragPermutationIndex);
      }
    }
  }
  if (!fragBlob->uniforms.empty()) {
    fragmentDesc.uniformSlots = {
        {std::string(FragmentUniformBlockName), static_cast<unsigned>(FRAGMENT_UBO_BINDING_POINT)}};
  }

#if DEBUG
  static const char* dumpDir = std::getenv("TGFX_AOT_DUMP_DIR");
#else
  const char* dumpDir = nullptr;
#endif
  if (dumpDir != nullptr) {
    std::string base = std::string(dumpDir) + "/" + matchResult->shaderName + "_v" +
                       std::to_string(matchResult->variant.vertPermutationIndex) + "_f" +
                       std::to_string(matchResult->variant.fragPermutationIndex);
    if (!vertexDesc.code.empty()) {
      std::string path = base + ".vert";
      FILE* file = fopen(path.c_str(), "w");
      if (file == nullptr) {
        LOGE("PrecompiledProgramCreator::CreateProgram() Cannot open dump file %s", path.c_str());
      } else {
        fwrite(vertexDesc.code.data(), 1, vertexDesc.code.size(), file);
        fclose(file);
      }
    }
    if (!fragmentDesc.code.empty()) {
      std::string path = base + ".frag";
      FILE* file = fopen(path.c_str(), "w");
      if (file == nullptr) {
        LOGE("PrecompiledProgramCreator::CreateProgram() Cannot open dump file %s", path.c_str());
      } else {
        fwrite(fragmentDesc.code.data(), 1, fragmentDesc.code.size(), file);
        fclose(file);
      }
    }
  }

  auto* shaderCache = cache->shaderCache();
  auto vertexShader = shaderCache->findOrCreateModule(vertexDesc);
  if (vertexShader == nullptr) {
    cache->recordFailure(PrecompiledFallbackReason::VertexModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledProgramCreator: Failed to create vertex shader module for %s[vert=%u]",
         matchResult->shaderName.c_str(), matchResult->variant.vertPermutationIndex);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  auto fragmentShader = shaderCache->findOrCreateModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    if (specInjected) {
      return FallbackToGeneric(context, programInfo, match, "fragment module");
    }
    cache->recordFailure(PrecompiledFallbackReason::FragmentModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledProgramCreator: Failed to create fragment shader module for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->variant.fragPermutationIndex);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::FragmentModuleCreated);

  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(programInfo->getVertexAttributes());
  auto& instanceAttributes = programInfo->getInstanceAttributes();
  // The AOT vertex stage declares its inputs at the template's physical slots, which keep holes
  // where a conditional attribute is compiled out of the variant (e.g. inUVCoord under
  // HAS_UV_MATRIX=1), while the GP's attribute list filters those slots away. Two numbering
  // conventions coexist: a template that declares one input per surviving attribute numbers them
  // densely, while one that always declares a slot for a filtered-away attribute numbers by the
  // physical slot. A one-to-one interface therefore pairs in order, and only a shader that reads a
  // strict subset of the attribute list is matched by physical slot. Instance inputs take the slots
  // after the vertex ones.
  const auto& attributeLocations = programInfo->getGeometryProcessor()->vertexAttributeLocations();
  uint32_t instanceBase = 0;
  for (auto location : attributeLocations) {
    instanceBase = std::max(instanceBase, location + 1);
  }
  if (!vertBlob->vertexInputs.empty()) {
    auto& inputs = vertBlob->vertexInputs;
    // Instance inputs occupy the slots after the vertex ones; everything below divides the shader's
    // declared inputs along that boundary.
    std::vector<const VertexInput*> vertexInputs = {};
    for (const auto& input : inputs) {
      if (input.location < instanceBase) {
        vertexInputs.push_back(&input);
        continue;
      }
      if (input.location >= instanceBase + instanceAttributes.size()) {
        cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
                             MakeFallbackRecord(cache, programInfo, &*matchResult));
        LOGE(
            "PrecompiledProgramCreator: vertex interface mismatch for %s[vert=%u]: declared input "
            "at location %u, the geometry processor provides no such attribute",
            matchResult->shaderName.c_str(), matchResult->variant.vertPermutationIndex,
            input.location);
        return nullptr;
      }
    }
    if (vertexInputs.size() > vertexLayout.attributes.size()) {
      cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
                           MakeFallbackRecord(cache, programInfo, &*matchResult));
      LOGE(
          "PrecompiledProgramCreator: vertex interface mismatch for %s[vert=%u]: %zu reflected "
          "vertex inputs vs %zu runtime attributes",
          matchResult->shaderName.c_str(), matchResult->variant.vertPermutationIndex,
          vertexInputs.size(), vertexLayout.attributes.size());
      return nullptr;
    }
    if (vertexInputs.size() == vertexLayout.attributes.size()) {
      for (const auto* input : vertexInputs) {
        vertexLayout.shaderLocations.push_back(input->location);
      }
    } else {
      // The kernel reads a subset: the attributes are shared across kernels, and a kernel that
      // takes its color from a uniform simply does not declare the color slot. Bind each attribute
      // to its physical slot where the shader declares it, and park the unread attributes on spare
      // locations past the declared range. A subset whose locations follow the dense convention
      // cannot be mapped this way, so it stays an interface error rather than binding wrongly.
      // The parked slots start after the instance slots: a shader that declares no vertex coverage
      // (ShapeInstancedTextureCoverage) would otherwise park the unread vertex attributes on the
      // very locations its instance attributes use.
      uint32_t spareLocation = instanceBase + static_cast<uint32_t>(instanceAttributes.size());
      for (auto location : attributeLocations) {
        bool read = std::any_of(
            vertexInputs.begin(), vertexInputs.end(),
            [location](const VertexInput* input) { return input->location == location; });
        vertexLayout.shaderLocations.push_back(read ? location : spareLocation++);
      }
      for (const auto* input : vertexInputs) {
        if (std::find(attributeLocations.begin(), attributeLocations.end(), input->location) ==
            attributeLocations.end()) {
          cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
                               MakeFallbackRecord(cache, programInfo, &*matchResult));
          LOGE(
              "PrecompiledProgramCreator: vertex interface mismatch for %s[vert=%u]: the shader "
              "reads a subset of the attributes but declares input at location %u, which the "
              "geometry processor does not provide",
              matchResult->shaderName.c_str(), matchResult->variant.vertPermutationIndex,
              input->location);
          return nullptr;
        }
      }
    }
  }
  descriptor.vertex.bufferLayouts = {vertexLayout};
  uint32_t instanceAttributeBase = instanceBase;
  if (!instanceAttributes.empty()) {
    VertexBufferLayout instanceLayout(instanceAttributes, VertexStepMode::Instance);
    for (size_t index = 0; index < instanceAttributes.size(); ++index) {
      instanceLayout.shaderLocations.push_back(instanceBase + static_cast<uint32_t>(index));
    }
    descriptor.vertex.bufferLayouts.push_back(instanceLayout);
  }
  // The location check above says the shader reads the attribute slots the GP provides; the
  // format check says it reads them with the layout the GP binds. Half-precision and normalized
  // byte attributes are read as their float forms by the precompiled GLSL, so those map onto the
  // float formats before the comparison; anything else must match exactly.
  {
    bool formatMismatch = false;
    for (size_t slot = 0; !formatMismatch && slot < vertexLayout.attributes.size() &&
                          slot < vertexLayout.shaderLocations.size();
         ++slot) {
      formatMismatch =
          !VertexInputFormatMatches(vertBlob->vertexInputs, vertexLayout.shaderLocations[slot],
                                    vertexLayout.attributes[slot].format(), matchResult->shaderName,
                                    cache, programInfo, &*matchResult);
    }
    for (size_t index = 0; !formatMismatch && index < instanceAttributes.size(); ++index) {
      formatMismatch = !VertexInputFormatMatches(
          vertBlob->vertexInputs, instanceAttributeBase + static_cast<uint32_t>(index),
          instanceAttributes[index].format(), matchResult->shaderName, cache, programInfo,
          &*matchResult);
    }
    if (formatMismatch) {
      return nullptr;
    }
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
  if (!vertBlob->uniforms.empty()) {
    vertexUniformData = std::unique_ptr<UniformData>(new UniformData(vertBlob->uniforms));
    vertexUniformData->buildParamLayout();
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (!fragBlob->uniforms.empty()) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(fragBlob->uniforms));
    fragmentUniformData->buildParamLayout();
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  int textureBinding = 0;
  for (const auto& sampler : fragBlob->samplers) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }

  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();

  auto pipeline = shaderCache->findOrCreatePipeline(descriptor);
  if (pipeline == nullptr) {
    if (specInjected) {
      return FallbackToGeneric(context, programInfo, match, "pipeline");
    }
    // The same shader variant (identical vert/frag artifact) can create successfully for some draws
    // and fail for others, so the mismatch must live in the runtime-derived pipeline state rather
    // than the compiled modules. Dump every state field the descriptor carries so a failing draw
    // can be diffed against a succeeding one (the backend logs the underlying error separately).
    std::ostringstream diag;
    diag << "PrecompiledProgramCreator: Failed to create render pipeline for "
         << matchResult->shaderName << "[vert=" << matchResult->variant.vertPermutationIndex
         << ",frag=" << matchResult->variant.fragPermutationIndex << "]";
    if (!descriptor.fragment.colorAttachments.empty()) {
      const auto& color = descriptor.fragment.colorAttachments[0];
      diag << " colorFormat=" << static_cast<int>(color.format)
           << " blendEnable=" << (color.blendEnable ? 1 : 0) << " blend=("
           << static_cast<int>(color.srcColorBlendFactor) << ","
           << static_cast<int>(color.dstColorBlendFactor) << ",op"
           << static_cast<int>(color.colorBlendOp) << " / "
           << static_cast<int>(color.srcAlphaBlendFactor) << ","
           << static_cast<int>(color.dstAlphaBlendFactor) << ",op"
           << static_cast<int>(color.alphaBlendOp) << ") writeMask=0x" << std::hex
           << color.colorWriteMask << std::dec;
    }
    diag << " sampleCount=" << descriptor.multisample.count
         << " depthStencilFormat=" << static_cast<int>(descriptor.depthStencil.format)
         << " depthCompare=" << static_cast<int>(descriptor.depthStencil.depthCompare)
         << " depthWrite=" << (descriptor.depthStencil.depthWriteEnabled ? 1 : 0)
         << " uniformBlocks=" << descriptor.layout.uniformBlocks.size()
         << " samplers=" << descriptor.layout.textureSamplers.size()
         << " vertexBuffers=" << descriptor.vertex.bufferLayouts.size();
    for (size_t i = 0; i < descriptor.vertex.bufferLayouts.size(); ++i) {
      const auto& layout = descriptor.vertex.bufferLayouts[i];
      diag << " buffer" << i << "(step=" << static_cast<int>(layout.stepMode)
           << ",stride=" << layout.stride << ",attrs=";
      for (const auto& attr : layout.attributes) {
        diag << attr.name() << ":" << static_cast<int>(attr.format()) << " ";
      }
      diag << ")";
    }
    LOGE("%s", diag.str().c_str());

    cache->recordFailure(PrecompiledFallbackReason::PipelineCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PipelineCreated,
                        MakeHitRecord(cache, programInfo, *matchResult));

  auto writer = MakeProgramWriter(*programInfo, matchResult->shaderName,
                                  matchResult->variant.vertPermutationIndex);
  if (writer == nullptr) {
    // A family without a writer has no way to upload its uniforms. The matcher only ever returns
    // table rows, so this is a defect rather than a draw the runtime can serve.
    cache->recordFailure(PrecompiledFallbackReason::Unspecified,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    return nullptr;
  }
  auto program = std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                           std::move(fragmentUniformData), std::move(writer));
  program->setShaderName(matchResult->shaderName);
  program->setExpectedTextureCount(static_cast<uint32_t>(descriptor.layout.textureSamplers.size()));
  return program;
}

}  // namespace tgfx
