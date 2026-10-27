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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ProfileEmitter.h"

#include <iostream>
#include <utility>
#include "FormCheck.h"
#include "GLSLDialect.h"

namespace tgfx {

namespace {

// SPIR-V words to the bytes a bundle stores: the modules are emitted as their u32 stream.
std::vector<uint8_t> WordsToBytes(const std::vector<uint32_t>& words) {
  auto* begin = reinterpret_cast<const uint8_t*>(words.data());
  return std::vector<uint8_t>(begin, begin + words.size() * 4);
}

// The Vulkan storage: the same SPIR-V as the common compile, but optimized for size.
class VulkanEmitter : public ProfileEmitter {
 public:
  ShaderProfile profile() const override {
    return ShaderProfile::Vulkan;
  }

  std::optional<EmittedStages> emit(const EmitInput& input, StageCompiler* compiler) override {
    // Re-compile with optimization for smaller SPIR-V output.
    auto expandedVertOpt = PrependDefines(*input.vertSource, *input.vertDefines);
    const auto& vertOpt = compiler->compileSpirv(expandedVertOpt, ShaderStageType::Vertex,
                                                 input.info->name, input.vertIndex, true);
    auto expandedFragOpt = PrependDefines(*input.fragSource, *input.fragDefines);
    const auto& fragOpt = compiler->compileSpirv(expandedFragOpt, ShaderStageType::Fragment,
                                                 input.info->name, input.fragIndex, true);
    if (!vertOpt.success || !fragOpt.success) {
      // An optimization failure is an error, not a downgrade: the bundle would silently store
      // a bigger module that no one asked to store. No current source triggers this — the
      // golden builds all optimize — so the fail-closed path rides along at no cost.
      std::cerr << "  optimized SPIR-V compilation failed for " << input.info->name
                << " [vert=" << input.vertIndex << " frag=" << input.fragIndex << "]: "
                << (vertOpt.success ? fragOpt.error : vertOpt.error) << "\n";
      return std::nullopt;
    }
    return EmittedStages{WordsToBytes(vertOpt.spirv), WordsToBytes(fragOpt.spirv)};
  }
};

// The Metal storage: MSL translated from the unoptimized SPIR-V, compiled to a metallib.
class MetalEmitter : public ProfileEmitter {
 public:
  ShaderProfile profile() const override {
    return ShaderProfile::Metal;
  }

  std::optional<EmittedStages> emit(const EmitInput& input, StageCompiler* compiler) override {
    // The unoptimized SPIR-V is intentional here: xcrun produces byte-identical metallibs
    // whether fed the optimized or unoptimized translation (verified 2026-08-19), so the
    // extra optimized compile would only slow the build down.
    std::string vertMslError;
    std::string fragMslError;
    auto mslVert = TranslateToMSL(*input.vertSpirv, ShaderStageType::Vertex, &vertMslError);
    auto mslFrag =
        TranslateToMSL(input.fragStage->spirv, ShaderStageType::Fragment, &fragMslError);
    if (mslVert.empty() || mslFrag.empty()) {
      std::cerr << "  MSL translation error: " << (mslVert.empty() ? vertMslError : fragMslError)
                << "\n";
      return std::nullopt;
    }
    std::string metalError;
    EmittedStages stages;
    stages.vertexBlob = compiler->compileMetallib(mslVert, ShaderStageType::Vertex, &metalError);
    if (!stages.vertexBlob.empty()) {
      stages.fragmentBlob =
          compiler->compileMetallib(mslFrag, ShaderStageType::Fragment, &metalError);
    }
    if (stages.vertexBlob.empty() || stages.fragmentBlob.empty()) {
      std::cerr << "  metallib compilation failed for " << input.info->name
                << " [vert=" << input.vertIndex << " frag=" << input.fragIndex
                << "]: " << metalError << "\n";
      return std::nullopt;
    }
    return stages;
  }
};

// The WebGPU storage: WGSL translated from the variant's own GLSL form (separated
// texture/sampler bindings), not from the combined-sampler SPIR-V the other backends share.
class WebGPUEmitter : public ProfileEmitter {
 public:
  ShaderProfile profile() const override {
    return ShaderProfile::WebGPU;
  }

  std::optional<EmittedStages> emit(const EmitInput& input, StageCompiler* compiler) override {
    // WebGPU needs its own GLSL form (separated texture/sampler bindings), so re-expand and
    // recompile like the vulkan branch instead of reusing the combined-sampler SPIR-V.
    auto expandedVertWgsl = PrependDefines(*input.vertSource, *input.vertDefines);
    auto expandedFragWgsl = PrependDefines(*input.fragSource, *input.fragDefines);
    const auto& wgslVert = compiler->compileWgsl(expandedVertWgsl, ShaderStageType::Vertex,
                                                 input.info->name, input.vertIndex);
    const auto& wgslFrag = compiler->compileWgsl(expandedFragWgsl, ShaderStageType::Fragment,
                                                 input.info->name, input.fragIndex);
    if (!wgslVert.success || !wgslFrag.success) {
      std::cerr << "  WGSL translation error: "
                << (wgslVert.success ? wgslFrag.error : wgslVert.error) << "\n";
      return std::nullopt;
    }
    EmittedStages stages;
    stages.vertexBlob.assign(wgslVert.text.begin(), wgslVert.text.end());
    stages.fragmentBlob.assign(wgslFrag.text.begin(), wgslFrag.text.end());
    return stages;
  }
};

// The GL and GLES storage: the template's own dialect spelling (GLSL 330 / ES 300) instead of
// text regenerated from SPIR-V, checked against its dialect's form contract before it is kept.
class DirectGLSLEmitter : public ProfileEmitter {
 public:
  explicit DirectGLSLEmitter(bool esDialect) : esDialect(esDialect) {
  }

  ShaderProfile profile() const override {
    return esDialect ? ShaderProfile::OpenGLES : ShaderProfile::OpenGL;
  }

  std::optional<EmittedStages> emit(const EmitInput& input, StageCompiler*) override;

 private:
  bool esDialect;
};

std::optional<EmittedStages> DirectGLSLEmitter::emit(const EmitInput& input, StageCompiler*) {
  EmittedStages stages;
  std::string directVert;
  std::string directFrag;
  if (esDialect) {
    // Direct ES-300 storage (see EmitDirectGLSLES300). The fbf variants carry the
    // framebuffer-fetch dialect: the fragment stage is translated with it.
    bool fbfVariant = input.info->fragDomain.valueOf(input.fragIndex, XP_DEFINE_NAME) ==
                      XP_FRAMEBUFFER_FETCH;
    directVert = EmitDirectGLSLES300(PrependDefines(*input.vertSource, *input.vertDefines),
                                     ShaderStageType::Vertex, false);
    directFrag = EmitDirectGLSLES300(PrependDefines(*input.fragSource, *input.fragDefines),
                                     ShaderStageType::Fragment, fbfVariant);
    if (directVert.empty() || directFrag.empty()) {
      std::cerr << "  direct-ES emission failed (preprocessing or missing #version 450) for "
                << input.info->name << " [vert=" << input.vertIndex
                << " frag=" << input.fragIndex << "]\n";
      return std::nullopt;
    }
  } else {
    // Desktop GL direct emission (see EmitDirectGLSL330): store the preprocessed template in
    // its own 330 spelling. The form check below fails the variant on any transform leak, so
    // a template using syntax beyond GL 330 (or a leak of Vulkan-only qualifiers like
    // subpassInput into a desktop variant) fails the build here instead of surfacing as a
    // runtime module-creation fallback.
    directVert = EmitDirectGLSL330(PrependDefines(*input.vertSource, *input.vertDefines),
                                   ShaderStageType::Vertex);
    directFrag = EmitDirectGLSL330(PrependDefines(*input.fragSource, *input.fragDefines),
                                   ShaderStageType::Fragment);
    if (directVert.empty() || directFrag.empty()) {
      std::cerr << "  direct-GL 330 emission failed (preprocessing or missing #version 450) for "
                << input.info->name << " [vert=" << input.vertIndex
                << " frag=" << input.fragIndex << "]\n";
      return std::nullopt;
    }
  }
  // The stored text is preprocessed, so the shared leak scan (FindDirectFormLeak) sees only
  // live code: the dialect header, no surviving set/binding member, no conditional directive,
  // and no Vulkan subpass syntax left after the framebuffer-fetch rewrite.
  auto vertLeak = FindDirectFormLeak(directVert, esDialect);
  auto fragLeak = FindDirectFormLeak(directFrag, esDialect);
  const DirectFormLeak* leak = vertLeak.reason != nullptr ? &vertLeak : &fragLeak;
  if (leak->reason != nullptr) {
    if (esDialect) {
      std::cerr << "  direct-ES form assertion failed (" << leak->reason << ") for "
                << input.info->name << " [vert=" << input.vertIndex
                << " frag=" << input.fragIndex << "]\n";
    } else {
      const char* leakStage = vertLeak.reason != nullptr ? "vert" : "frag";
      const std::string& leakText = vertLeak.reason != nullptr ? directVert : directFrag;
      std::cerr << "  direct-GL 330 form assertion failed (" << leakStage << ", " << leak->reason
                << ") for " << input.info->name << " [vert=" << input.vertIndex
                << " frag=" << input.fragIndex << "]";
      if (leak->offset > 0) {
        auto echoStart = leak->offset > 40 ? leak->offset - 40 : 0;
        std::cerr << " [text @ " << leakText.substr(echoStart, 90) << "]";
      }
      std::cerr << "\n";
    }
    return std::nullopt;
  }
  stages.vertexBlob.assign(directVert.begin(), directVert.end());
  stages.fragmentBlob.assign(directFrag.begin(), directFrag.end());
  return stages;
}

}  // namespace

std::unique_ptr<ProfileEmitter> MakeProfileEmitter(ShaderProfile profile) {
  switch (profile) {
    case ShaderProfile::Vulkan:
      return std::make_unique<VulkanEmitter>();
    case ShaderProfile::Metal:
      return std::make_unique<MetalEmitter>();
    case ShaderProfile::WebGPU:
      return std::make_unique<WebGPUEmitter>();
    case ShaderProfile::OpenGL:
      return std::make_unique<DirectGLSLEmitter>(false);
    case ShaderProfile::OpenGLES:
      return std::make_unique<DirectGLSLEmitter>(true);
  }
  return nullptr;
}

}  // namespace tgfx
