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
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatch.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramSignature.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ShaderKeyHash.h"
#include "gpu/UniformData.h"
#include "gpu/aot/AOTEffectDecomposer.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/ShaderFamily.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Parses a decimal int the way std::stoi accepts it (leading whitespace, an optional sign, digits,
// anything after the digits ignored), but reports failure through the return value. Builds that
// disable exceptions (Android, web) cannot use std::stoi: it signals bad input by throwing.
static bool ParseInt(const std::string& text, int* value) {
  const char* begin = text.c_str();
  char* end = nullptr;
  errno = 0;
  long parsed = std::strtol(begin, &end, 10);
  if (end == begin || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

static ShaderCodeFormat FormatForBackend(Backend backend) {
  switch (backend) {
    case Backend::Vulkan:
      return ShaderCodeFormat::SPIRV;
    case Backend::Metal:
      return ShaderCodeFormat::MSL;
    case Backend::WebGPU:
      return ShaderCodeFormat::WGSL;
    default:
      return ShaderCodeFormat::GLSL;
  }
}

static ShaderArtifactOrigin ArtifactOriginForBackend(
    Backend backend, const ShaderModuleDescriptor& vertexDescriptor,
    const ShaderModuleDescriptor& fragmentDescriptor) {
  auto hasOfflineBinary =
      !vertexDescriptor.binaryData.empty() && !fragmentDescriptor.binaryData.empty();
  if ((backend == Backend::Vulkan || backend == Backend::Metal) && hasOfflineBinary) {
    return ShaderArtifactOrigin::OfflineBinary;
  }
  return ShaderArtifactOrigin::OfflineSource;
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

static std::string ProgramKeyForDiagnostics(const ProgramInfo* programInfo) {
  return ProgramInfo::FormatDiagnosticKey(programInfo->programKeyForDiagnostics());
}

static PrecompiledFallbackRecord MakeFallbackRecord(
    const PrecompiledShaderCache* cache, const ProgramInfo* programInfo,
    const PermutationMatchResult* matchResult = nullptr) {
  PrecompiledFallbackRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramKeyForDiagnostics(programInfo);
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  if (matchResult != nullptr) {
    record.shaderName = matchResult->shaderName;
    record.vertPermutationIndex = matchResult->vertPermutationIndex;
    record.fragPermutationIndex = matchResult->fragPermutationIndex;
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
  record.programKey = ProgramKeyForDiagnostics(programInfo);
  record.effectSignature = BuildEffectSignature(programInfo);
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  record.shaderName = matchResult.shaderName;
  record.vertPermutationIndex = matchResult.vertPermutationIndex;
  record.fragPermutationIndex = matchResult.fragPermutationIndex;
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
      shaderName.c_str(), matchResult->vertPermutationIndex, location, static_cast<int>(reflected),
      static_cast<int>(runtimeFormat));
  return false;
}

// The two re-expressing families carry their own writers (they rebuild the draw's effect as the
// fused chain or perlin processor every draw); every other family uses the generic precompiled
// writer.
static std::unique_ptr<ProgramWriter> MakeProgramWriter(const std::string& shaderName) {
  if (shaderName == PointwiseChainShader::Name) {
    return std::make_unique<ChainProgramWriter>();
  }
  if (shaderName == PerlinNoiseFillShader::Name) {
    return std::make_unique<PerlinProgramWriter>();
  }
  return std::make_unique<PrecompiledProgramWriter>();
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgram(Context* context,
                                                                  const ProgramInfo* programInfo,
                                                                  bool* deferredFallback) {
  if (deferredFallback != nullptr) {
    *deferredFallback = false;
  }
  auto cache = context->precompiledShaderCache();
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  if (!cache->isLoaded()) {
    // Say why nothing is loaded: a backend or build with no bundle is by design, a rejected bundle
    // is a defect that needs regenerating. Anything else (TGFX_AOT_DISABLE, a test that unloaded
    // the cache) keeps the generic reason.
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

  PermutationMatchFailure matchFailure = PermutationMatchFailure::None;
  auto matchResult = MatchPermutation(programInfo, &matchFailure);
  if (!matchResult) {
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
          if (deferredFallback != nullptr) {
            *deferredFallback = true;
          }
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
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);
  return CreateProgramFromMatch(context, programInfo, *matchResult);
}

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgramFromMatch(
    Context* context, const ProgramInfo* programInfo, const PermutationMatchResult& match) {
  auto cache = context->precompiledShaderCache();
  // The callers hold their own match in hand (CreateProgram just ran the walk; the AOT route
  // derives its variant from the compiled program), so this entry starts past the match.
  const PermutationMatchResult* matchResult = &match;

  // TGFX_AOT_DENY holds a comma-separated list of shader names whose precompiled match is
  // rejected so the draw falls back to the runtime JIT program. Diagnostic only: it isolates a
  // single kernel family in A/B pixel diffs. Parsed once per process: a token of the form
  // "name:vert:frag" additionally pins the permutation indices, so a single variant of a kernel
  // family can be isolated; a bare "name" denies the whole family; anything malformed never
  // matches.
  struct DenyToken {
    std::string name;
    bool pinned = false;
    uint32_t vertIndex = 0;
    uint32_t fragIndex = 0;
  };
  struct DenyList {
    std::vector<DenyToken> tokens;
    DenyList() {
      const char* value = std::getenv("TGFX_AOT_DENY");
      if (value == nullptr) {
        return;
      }
      const std::string list(value);
      size_t begin = 0;
      while (begin <= list.size()) {
        const size_t end = list.find(',', begin);
        const std::string token =
            list.substr(begin, end == std::string::npos ? list.size() - begin : end - begin);
        begin = end == std::string::npos ? list.size() + 1 : end + 1;
        if (token.empty()) {
          continue;
        }
        const size_t sep = token.find(':');
        if (sep == std::string::npos) {
          tokens.push_back(DenyToken{token, false, 0, 0});
          continue;
        }
        const size_t sep2 = token.find(':', sep + 1);
        if (sep2 == std::string::npos) {
          continue;
        }
        DenyToken parsed = {};
        parsed.name = token.substr(0, sep);
        int vertIndex = 0;
        int fragIndex = 0;
        if (!ParseInt(token.substr(sep + 1, sep2 - sep - 1), &vertIndex) ||
            !ParseInt(token.substr(sep2 + 1), &fragIndex)) {
          continue;
        }
        parsed.pinned = true;
        parsed.vertIndex = static_cast<uint32_t>(vertIndex);
        parsed.fragIndex = static_cast<uint32_t>(fragIndex);
        tokens.push_back(std::move(parsed));
      }
    }
  };
  static const DenyList denied;
  for (const auto& token : denied.tokens) {
    const bool matched = token.name == matchResult->shaderName &&
                         (!token.pinned || (token.vertIndex == matchResult->vertPermutationIndex &&
                                            token.fragIndex == matchResult->fragPermutationIndex));
    if (matched) {
      cache->recordArtifactMiss(PrecompiledFallbackReason::Unspecified,
                                MakeFallbackRecord(cache, programInfo, &*matchResult));
      return nullptr;
    }
  }

  auto vertHash = ComputeVertexKeyHash(matchResult->shaderName, matchResult->vertPermutationIndex,
                                       cache->profileTag());
  auto fragHash = ComputeFragmentKeyHash(matchResult->shaderName, matchResult->fragPermutationIndex,
                                         cache->profileTag());

  auto vertBlob = cache->findVertex(vertHash.hi, vertHash.lo);
  if (vertBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: vert blob not found for %s[vert=%u]",
         matchResult->shaderName.c_str(), matchResult->vertPermutationIndex);
    return nullptr;
  }
  auto fragBlob = cache->findFragment(fragHash.hi, fragHash.lo);
  if (fragBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::FragmentArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledShaderMiss: frag blob not found for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->fragPermutationIndex);
    return nullptr;
  }
  cache->recordArtifactHit();

  auto format = FormatForBackend(context->backend());

  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = format;
  vertexDesc.stage = ShaderStage::Vertex;
  if (format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::MSL) {
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
  if (format == ShaderCodeFormat::SPIRV || format == ShaderCodeFormat::MSL) {
    fragmentDesc.binaryData = fragBlob->data;
  } else {
    fragmentDesc.code = std::string(fragBlob->data.begin(), fragBlob->data.end());
  }
  if (!fragBlob->uniforms.empty()) {
    fragmentDesc.uniformSlots = {
        {std::string(FragmentUniformBlockName), static_cast<unsigned>(FRAGMENT_UBO_BINDING_POINT)}};
  }

  static const char* dumpDir = std::getenv("TGFX_AOT_DUMP_DIR");
  if (dumpDir != nullptr) {
    std::string base = std::string(dumpDir) + "/" + matchResult->shaderName + "_v" +
                       std::to_string(matchResult->vertPermutationIndex) + "_f" +
                       std::to_string(matchResult->fragPermutationIndex);
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
         matchResult->shaderName.c_str(), matchResult->vertPermutationIndex);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  auto fragmentShader = shaderCache->findOrCreateModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    cache->recordFailure(PrecompiledFallbackReason::FragmentModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &*matchResult));
    LOGE("PrecompiledProgramCreator: Failed to create fragment shader module for %s[frag=%u]",
         matchResult->shaderName.c_str(), matchResult->fragPermutationIndex);
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
            matchResult->shaderName.c_str(), matchResult->vertPermutationIndex, input.location);
        return nullptr;
      }
    }
    if (vertexInputs.size() > vertexLayout.attributes.size()) {
      cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
                           MakeFallbackRecord(cache, programInfo, &*matchResult));
      LOGE(
          "PrecompiledProgramCreator: vertex interface mismatch for %s[vert=%u]: %zu reflected "
          "vertex inputs vs %zu runtime attributes",
          matchResult->shaderName.c_str(), matchResult->vertPermutationIndex, vertexInputs.size(),
          vertexLayout.attributes.size());
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
      // (ShapeInstancedTextureCoverageShader) would otherwise park the unread vertex attributes on
      // the very locations its instance attributes use.
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
              matchResult->shaderName.c_str(), matchResult->vertPermutationIndex, input->location);
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
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());

  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;
  if (!vertBlob->uniforms.empty()) {
    vertexUniformData = std::unique_ptr<UniformData>(new UniformData(vertBlob->uniforms));
    vertexUniformData->skipSuffix = true;
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (!fragBlob->uniforms.empty()) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(fragBlob->uniforms));
    fragmentUniformData->skipSuffix = true;
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

  bool pipelineCreated = false;
  auto pipeline = shaderCache->findOrCreatePipeline(descriptor, &pipelineCreated);
  if (pipelineCreated) {
    context->globalCache()->recordRuntimePipelineCreation(pipeline != nullptr);
  }
  if (pipeline == nullptr) {
    // The same shader variant (identical vert/frag artifact) can create successfully for some draws
    // and fail for others, so the mismatch must live in the runtime-derived pipeline state rather
    // than the compiled modules. Dump every state field the descriptor carries so a failing draw
    // can be diffed against a succeeding one (the backend logs the underlying error separately).
    std::ostringstream diag;
    diag << "PrecompiledProgramCreator: Failed to create render pipeline for "
         << matchResult->shaderName << "[vert=" << matchResult->vertPermutationIndex
         << ",frag=" << matchResult->fragPermutationIndex << "]";
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

  ProgramProvenance provenance = {
      ArtifactOriginForBackend(context->backend(), vertexDesc, fragmentDesc),
      ProgramOrigin::PrecompiledArtifact, PipelineOrigin::RuntimeCreation};
  auto program = std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                           std::move(fragmentUniformData), provenance,
                                           MakeProgramWriter(matchResult->shaderName));
  program->setShaderName(matchResult->shaderName);
  program->setExpectedTextureCount(static_cast<uint32_t>(descriptor.layout.textureSamplers.size()));
  return program;
}

}  // namespace tgfx
