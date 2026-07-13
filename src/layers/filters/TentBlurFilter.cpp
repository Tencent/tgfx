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

#include "tgfx/layers/filters/TentBlurFilter.h"
#include <cmath>
#include <string>
#include "core/utils/Log.h"
#include "layers/RuntimeEffectUtils.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Sampler.h"

namespace tgfx {

// --- TentBlurEffect: single-pass RuntimeEffect for separable tent blur ---

static constexpr char TENT_BLUR_VERTEX_SHADER[] = R"(
    in vec2 aPosition;
    in vec2 aTextureCoord;
    out vec2 vTexCoord;
    void main() {
        gl_Position = vec4(aPosition, 0.0, 1.0);
        vTexCoord = aTextureCoord;
    }
)";

static constexpr char TENT_BLUR_FRAGMENT_SHADER[] = R"(
    precision highp float;
    in vec2 vTexCoord;
    uniform sampler2D uSource;
    layout(std140) uniform TentBlurUniforms {
        vec2 uBlurDelta;
        float uRadius;
        float uStepLength;
    };
    out vec4 tgfx_FragColor;

    void main() {
        float radius = floor(uRadius);
        if (radius < 1.0) {
            tgfx_FragColor = texture(uSource, vTexCoord);
            return;
        }

        // Center sample: weight = radius
        float accumulated = texture(uSource, vTexCoord).a * radius;
        float weight = radius;

        // Iterate by 2 * stepLength: use hardware linear interpolation to blend adjacent taps.
        // Triangular kernel: w(d) = max(0, radius - |d|)
        // When stepLength > 1, sampling covers a wider area with fewer iterations.
        float stepLength = max(1.0, floor(uStepLength));
        for (float i = stepLength; i <= radius; i += 2.0 * stepLength) {
            float offsetA = i;
            float offsetB = min(i + stepLength, radius);

            float weightA = radius - offsetA;
            float weightB = radius - offsetB;
            float combinedWeight = weightA + weightB;

            // Interpolated sample position (weighted average of A and B)
            float interpOffset = (offsetA * weightA + offsetB * weightB) / combinedWeight;

            vec2 delta = uBlurDelta * interpOffset;
            accumulated += combinedWeight * (
                texture(uSource, vTexCoord - delta).a +
                texture(uSource, vTexCoord + delta).a
            );
            weight += 2.0 * combinedWeight;
        }

        float result = accumulated / max(weight, 0.0000001);
        tgfx_FragColor = vec4(result);
    }
)";

static constexpr int TENT_BLUR_VERTEX_SIZE = 16 * sizeof(float);
static constexpr int TENT_BLUR_MAX_RADIUS = 256;
static constexpr int TENT_BLUR_MAX_ITERATIONS = 32;

// Maps TileMode to AddressMode for sampler configuration.
static AddressMode TileModeToAddressMode(TileMode tileMode) {
  switch (tileMode) {
    case TileMode::Repeat:
      return AddressMode::Repeat;
    case TileMode::Mirror:
      return AddressMode::MirrorRepeat;
    case TileMode::Clamp:
    case TileMode::Decal:
      // Decal maps to ClampToEdge: ClampToBorder has limited GPU support, and for UDF
      // generation the edge alpha (0 or 1) clamped is the correct distance field behavior.
      return AddressMode::ClampToEdge;
  }
}

class TentBlurEffect : public RuntimeEffect {
 public:
  TentBlurEffect(float radius, bool horizontal, TileMode tileMode = TileMode::Clamp)
      : RuntimeEffect({}), _horizontal(horizontal), _tileMode(tileMode) {
    _effectiveRadius = std::min(radius, static_cast<float>(TENT_BLUR_MAX_RADIUS));
    // Cap iterations at TENT_BLUR_MAX_ITERATIONS by increasing the sampling step.
    // Each iteration covers 2 * stepLength pixels (one pair of taps).
    _stepLength = std::max(1.0f, std::ceil(_effectiveRadius / (TENT_BLUR_MAX_ITERATIONS * 2.0f)));
  }

  Rect filterBounds(const Rect& srcRect, MapDirection) const override {
    return srcRect.makeOutset(_effectiveRadius, _effectiveRadius);
  }

 protected:
  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;

  float _effectiveRadius = 0.0f;
  float _stepLength = 1.0f;
  bool _horizontal;
  TileMode _tileMode = TileMode::Clamp;
  // onDraw is a virtual const method in RuntimeEffect and cannot be made non-const.
  // Pipeline and sampler creation is lazy initialization that does not affect the filter's
  // logical state.
  mutable std::shared_ptr<RenderPipeline> cachedPipeline = nullptr;
  mutable std::shared_ptr<Sampler> cachedSampler = nullptr;
};

static std::string GetShaderVersion(bool isDesktop) {
  return isDesktop ? "#version 150\n\n" : "#version 300 es\n\n";
}

std::shared_ptr<RenderPipeline> TentBlurEffect::createPipeline(GPU* gpu) const {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = GetShaderVersion(isDesktop) + TENT_BLUR_VERTEX_SHADER;
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = GetShaderVersion(isDesktop) + TENT_BLUR_FRAGMENT_SHADER;
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(
      {{"aPosition", VertexFormat::Float2}, {"aTextureCoord", VertexFormat::Float2}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.layout.textureSamplers.push_back({"uSource", 0});
  BindingEntry uniformBinding = {"TentBlurUniforms", 0};
  descriptor.layout.uniformBlocks.push_back(uniformBinding);
  return gpu->createRenderPipeline(descriptor);
}

bool TentBlurEffect::onDraw(CommandEncoder* encoder,
                            const std::vector<std::shared_ptr<Texture>>& inputTextures,
                            std::shared_ptr<Texture> outputTexture, const Point& offset) const {
  if (inputTextures.empty()) {
    LOGE("TentBlurEffect: no input textures!");
    return false;
  }
  auto gpu = encoder->gpu();
  if (cachedPipeline == nullptr) {
    cachedPipeline = createPipeline(gpu);
    if (cachedPipeline == nullptr) {
      LOGE("TentBlurEffect: failed to create pipeline!");
      return false;
    }
  }

  RenderPassDescriptor renderPassDesc(outputTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    return false;
  }
  renderPass->setPipeline(cachedPipeline);

  auto vertexBuffer = gpu->createBuffer(TENT_BLUR_VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }
  CollectFullQuadVertices(inputTextures[0].get(), outputTexture.get(), offset, vertices);
  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);

  if (!cachedSampler) {
    auto addressMode = TileModeToAddressMode(_tileMode);
    SamplerDescriptor samplerDesc(addressMode, addressMode, FilterMode::Linear, FilterMode::Linear,
                                  MipmapMode::None);
    cachedSampler = gpu->createSampler(samplerDesc);
  }
  renderPass->setTexture(0, inputTextures[0], cachedSampler);

  // Uniform: vec2 uBlurDelta, float uRadius
  // std140 layout: vec2 + float + pad = 4 floats = 16 bytes
  auto sourceWidth = static_cast<float>(inputTextures[0]->width());
  auto sourceHeight = static_cast<float>(inputTextures[0]->height());
  size_t uniformSize = 4 * sizeof(float);
  auto uniformBuffer = gpu->createBuffer(uniformSize, GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    return false;
  }
  auto uniformData = static_cast<float*>(uniformBuffer->map());
  if (uniformData == nullptr) {
    return false;
  }
  if (_horizontal) {
    uniformData[0] = 1.0f / sourceWidth;
    uniformData[1] = 0.0f;
  } else {
    uniformData[0] = 0.0f;
    uniformData[1] = 1.0f / sourceHeight;
  }
  uniformData[2] = _effectiveRadius;
  uniformData[3] = _stepLength;
  uniformBuffer->unmap();
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformSize);

  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  return true;
}

// --- TentBlurFilter implementation ---

std::shared_ptr<ImageFilter> TentBlurFilter::MakeImageFilter(float radius, TileMode tileMode) {
  if (radius < 1.0f) {
    return nullptr;
  }
  auto hEffect = std::make_shared<TentBlurEffect>(radius, true, tileMode);
  auto vEffect = std::make_shared<TentBlurEffect>(radius, false, tileMode);
  auto hFilter = ImageFilter::Runtime(hEffect);
  auto vFilter = ImageFilter::Runtime(vEffect);
  return ImageFilter::Compose(hFilter, vFilter);
}

std::shared_ptr<TentBlurFilter> TentBlurFilter::Make(float blurrinessX, float blurrinessY,
                                                     TileMode tileMode) {
  return std::shared_ptr<TentBlurFilter>(new TentBlurFilter(blurrinessX, blurrinessY, tileMode));
}

void TentBlurFilter::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void TentBlurFilter::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void TentBlurFilter::setTileMode(TileMode tileMode) {
  if (_tileMode == tileMode) {
    return;
  }
  _tileMode = tileMode;
  invalidateFilter();
}

TentBlurFilter::TentBlurFilter(float blurrinessX, float blurrinessY, TileMode tileMode)
    : _blurrinessX(blurrinessX), _blurrinessY(blurrinessY), _tileMode(tileMode) {
}

std::shared_ptr<ImageFilter> TentBlurFilter::onCreateImageFilter(float scale) {
  auto radiusX = _blurrinessX * scale;
  auto radiusY = _blurrinessY * scale;
  if (radiusX < 1.0f && radiusY < 1.0f) {
    return nullptr;
  }

  auto hRadius = std::max(radiusX, 1.0f);
  auto vRadius = std::max(radiusY, 1.0f);

  auto hEffect = std::make_shared<TentBlurEffect>(hRadius, true, _tileMode);
  auto vEffect = std::make_shared<TentBlurEffect>(vRadius, false, _tileMode);

  auto hFilter = ImageFilter::Runtime(hEffect);
  auto vFilter = ImageFilter::Runtime(vEffect);

  return ImageFilter::Compose(hFilter, vFilter);
}

}  // namespace tgfx
