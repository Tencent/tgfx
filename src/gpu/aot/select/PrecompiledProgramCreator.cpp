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
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/aot/plan/DrawPlan.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/aot/write/ProgramWriterFactory.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/Layout.h"
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

std::string BuildEffectSignature(const GeometryLayout& geometry,
                                 const std::vector<const FragmentProcessor*>& colorProcessors,
                                 const std::vector<const FragmentProcessor*>& coverageProcessors,
                                 const XferFacts& xfer) {
  std::stringstream stream;
  stream << "GP=" << GeometryKindName(geometry.parameters.kind) << ";ColorFP=[";
  for (size_t i = 0; i < colorProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, colorProcessors[i]);
  }
  stream << "];CoverageFP=[";
  for (size_t i = 0; i < coverageProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, coverageProcessors[i]);
  }
  stream << "];XP=" << (xfer.shaderBlend() ? "shader" : "fixed");
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
  stream << programInfo->getEffectSignature()
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

static PrecompiledFallbackRecord MakeFallbackRecord(const PrecompiledShaderCache* cache,
                                                    const ProgramInfo* programInfo,
                                                    const Variant* variant = nullptr) {
  PrecompiledFallbackRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = programInfo->getEffectSignature();
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  if (variant != nullptr) {
    record.shaderName = Families()[variant->family].name;
    record.variantId = variant->id();
  }
  return record;
}

static PrecompiledHitRecord MakeHitRecord(const PrecompiledShaderCache* cache,
                                          const ProgramInfo* programInfo, const Variant& variant) {
  PrecompiledHitRecord record;
  if (!cache->diagnosticRecordingEnabled()) {
    return record;
  }
  record.programKey = ProgramInfo::FormatDiagnosticKey(programInfo->programKey());
  record.effectSignature = programInfo->getEffectSignature();
  record.pipelineSignature = BuildPipelineSignature(programInfo);
  record.shaderName = Families()[variant.family].name;
  record.variantId = variant.id();
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
  cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
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

  // The negative cache answers repeated misses. An empty table cannot answer anything, so the
  // whole-expression attempt key is only hashed when there is something to look it up in.
  if (cache->hasProgramMisses()) {
    PrecompiledFallbackReason cachedReason = PrecompiledFallbackReason::Unspecified;
    if (cache->findProgramMiss(programInfo->programKey(), &cachedReason)) {
      // The first draw with this key walked the rules and was refused; every later draw carrying
      // the same key gets the same answer, so skip the walk (and the record formatting that goes
      // with it). The cache drops the entry whenever the bundle changes.
      cache->recordCachedMiss(cachedReason);
      return nullptr;
    }
  }

  PermutationMatchFailure matchFailure = PermutationMatchFailure::None;
  auto selection = SelectProgram(context, *programInfo, &matchFailure);
  if (!selection) {
    auto reason = ToFallbackReason(matchFailure);
    if (reason == PrecompiledFallbackReason::NoMatchingRule) {
      // A sampler whose proxy resolves no view yet (a lazy/deferred upload) drops its content
      // through the runtime stub program by design; classify it apart from real coverage gaps so
      // the production metrics stay honest. The record rides the IR, not the processor list.
      const auto* ir = programInfo->getCarriedIR();
      if (ir != nullptr) {
        for (const auto& proxy : ir->textures) {
          if (proxy != nullptr && proxy->getTextureView() == nullptr) {
            reason = PrecompiledFallbackReason::DeferredTexture;
            break;
          }
        }
      }
    }
    auto record = MakeFallbackRecord(cache, programInfo);
    record.matchFailure = static_cast<uint8_t>(matchFailure);
    cache->recordArtifactMiss(reason, record);
    if (reason != PrecompiledFallbackReason::DeferredTexture) {
      // A deferred texture may be instantiated later, which can make the same key serviceable:
      // only the definitive refusals enter the negative cache.
      cache->recordProgramMiss(programInfo->programKey(), reason);
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
  const BytesKey key = MakeProgramKey(context, *programInfo, resolved.variant.id(), resolved.spec);
  if (auto cached = context->globalCache()->findProgram(key)) {
    return cached;
  }
  auto program = CreateProgramForVariant(context, programInfo, resolved.variant,
                                         resolved.spec.empty() ? nullptr : &resolved.spec);
  if (program != nullptr) {
    // A specialization that failed to build still returns the generic form (see the fallback in
    // CreateProgramForVariant); caching it under this key keeps the failing compile from being
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
                                           const Variant& variant, const char* stage) {
  LOGE(
      "PrecompiledProgramCreator: the specialized %s of %s[variant=%u] failed to build; falling "
      "back to the generic form",
      stage, Families()[variant.family].name, variant.id());
  return PrecompiledProgramCreator::CreateProgramForVariant(context, programInfo, variant, nullptr);
}

}  // namespace

std::shared_ptr<Program> PrecompiledProgramCreator::CreateProgramForVariant(
    Context* context, const ProgramInfo* programInfo, const Variant& variant,
    const SpecValues* spec) {
  auto cache = ContextCacheAccess::Cache(context);
  // The callers hold their own resolved variant in hand (CreateProgram just ran the walk; the AOT
  // route derives its variant from the compiled program), so this entry starts past the match.
  const char* shaderName = Families()[variant.family].name;
  const uint32_t variantId = variant.id();

  auto vertBlob = cache->findVertex(variantId);
  if (vertBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing,
                              MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledShaderMiss: vert blob not found for %s[variant=%u]", shaderName, variantId);
    return nullptr;
  }
  auto fragBlob = cache->findFragment(variantId);
  if (fragBlob == nullptr) {
    cache->recordArtifactMiss(PrecompiledFallbackReason::FragmentArtifactMissing,
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
  // The format's injector decides where the values land: the driver-source formats rewrite the
  // generated TGFX_SPEC_<id> declarations (the driver's compiler then folds every accessor read
  // and the unrolled executor's guards), the container formats append the pipeline's constant
  // table. A format without a registered injector serves the generic form.
  bool specInjected = false;
  std::vector<std::pair<std::string, double>> specConstants = {};
  if (spec != nullptr && !spec->empty()) {
    if (const auto* injector = FindSpecInjector(format)) {
      SpecInjection injection = {};
      if (injector->Apply(fragmentDesc.code, DeriveSpecFieldInfos(variant), *spec, &injection)) {
        if (!injection.code.empty()) {
          fragmentDesc.code = std::move(injection.code);
        }
        specConstants = std::move(injection.constants);
        specInjected = true;
      } else {
        LOGE(
            "PrecompiledProgramCreator: specialization injection found no TGFX_SPEC declarations "
            "for %s[variant=%u]; serving the generic form",
            shaderName, variantId);
      }
    }
  }
  if (layout.fragmentBlockSize > 0) {
    fragmentDesc.uniformSlots = {
        {std::string(FragmentUniformBlockName), static_cast<unsigned>(FRAGMENT_UBO_BINDING_POINT)}};
  }

  auto* shaderCache = cache->shaderCache();
  auto vertexShader = shaderCache->findOrCreateModule(vertexDesc);
  if (vertexShader == nullptr) {
    cache->recordFailure(PrecompiledFallbackReason::VertexModuleCreationFailed,
                         MakeFallbackRecord(cache, programInfo, &variant));
    LOGE("PrecompiledProgramCreator: Failed to create vertex shader module for %s[variant=%u]",
         shaderName, variantId);
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  auto fragmentShader = shaderCache->findOrCreateModule(fragmentDesc);
  if (fragmentShader == nullptr) {
    if (specInjected) {
      return FallbackToGeneric(context, programInfo, variant, "fragment module");
    }
    cache->recordFailure(PrecompiledFallbackReason::FragmentModuleCreationFailed,
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
    cache->recordFailure(PrecompiledFallbackReason::VertexLayoutMismatch,
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
  if (layout.fragmentBlockSize > 0) {
    fragmentUniformData = std::unique_ptr<UniformData>(new UniformData(layout.fragmentBlockSize));
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  for (const auto& texture : layout.textures) {
    descriptor.layout.textureSamplers.emplace_back(texture.name, texture.binding);
  }

  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();

  auto pipeline = shaderCache->findOrCreatePipeline(descriptor);
  if (pipeline == nullptr) {
    if (specInjected) {
      return FallbackToGeneric(context, programInfo, variant, "pipeline");
    }
    // The same shader variant (identical vert/frag artifact) can create successfully for some draws
    // and fail for others, so the mismatch must live in the runtime-derived pipeline state rather
    // than the compiled modules. Dump every state field the descriptor carries so a failing draw
    // can be diffed against a succeeding one (the backend logs the underlying error separately).
    std::ostringstream diag;
    diag << "PrecompiledProgramCreator: Failed to create render pipeline for " << shaderName
         << "[variant=" << variantId << "]";
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
                         MakeFallbackRecord(cache, programInfo, &variant));
    return nullptr;
  }
  cache->recordAOTStage(PrecompiledAOTStage::PipelineCreated,
                        MakeHitRecord(cache, programInfo, variant));

  auto writer = MakeProgramWriter(*programInfo, variant);
  if (writer == nullptr) {
    // A family without a writer has no way to upload its uniforms. The matcher only ever returns
    // table rows, so this is a defect rather than a draw the runtime can serve.
    cache->recordFailure(PrecompiledFallbackReason::Unspecified,
                         MakeFallbackRecord(cache, programInfo, &variant));
    return nullptr;
  }
  auto program = std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                           std::move(fragmentUniformData), std::move(writer));
  program->setShaderName(shaderName);
  program->setExpectedTextureCount(static_cast<uint32_t>(descriptor.layout.textureSamplers.size()));
  return program;
}

}  // namespace tgfx
