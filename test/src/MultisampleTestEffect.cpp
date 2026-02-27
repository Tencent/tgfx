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

#include "MultisampleTestEffect.h"
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Vertex shader: draws a diagonal triangle covering roughly the upper-left half of the output.
// The vertex color (including alpha) is passed through from the vertex attribute.
static constexpr char VERTEX_SHADER[] = R"(
        in vec2 aPosition;
        in vec4 aColor;
        out vec4 vertexColor;
        void main() {
            gl_Position = vec4(aPosition, 0.0, 1.0);
            vertexColor = aColor;
        }
    )";

// Fragment shader: outputs the interpolated vertex color directly.
// When alphaToCoverage is enabled, the alpha value drives the coverage mask.
static constexpr char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec4 vertexColor;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = vertexColor;
        }
    )";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

std::shared_ptr<MultisampleTestEffect> MultisampleTestEffect::Make(
    const MultisampleConfig& config) {
  return std::make_shared<MultisampleTestEffect>(config);
}

MultisampleTestEffect::MultisampleTestEffect(const MultisampleConfig& config) : config(config) {
}

Rect MultisampleTestEffect::filterBounds(const Rect& srcRect, MapDirection mapDirection) const {
  if (mapDirection == MapDirection::Reverse) {
    const auto largeSize = static_cast<float>(1 << 29);
    return Rect::MakeLTRB(-largeSize, -largeSize, largeSize, largeSize);
  }
  return srcRect;
}

std::shared_ptr<RenderPipeline> MultisampleTestEffect::createPipeline(GPU* gpu) const {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = GetFinalShaderCode(VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = GetFinalShaderCode(FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(
      {{"aPosition", VertexFormat::Float2}, {"aColor", VertexFormat::Float4}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.multisample.count = config.sampleCount;
  descriptor.multisample.mask = config.sampleMask;
  descriptor.multisample.alphaToCoverageEnabled = config.alphaToCoverage;
  return gpu->createRenderPipeline(descriptor);
}

bool MultisampleTestEffect::onDraw(CommandEncoder* encoder,
                                   const std::vector<std::shared_ptr<Texture>>&,
                                   std::shared_ptr<Texture> outputTexture,
                                   const Point&) const {
  auto gpu = encoder->gpu();
  auto pipeline = createPipeline(gpu);
  if (pipeline == nullptr) {
    return false;
  }

  std::shared_ptr<Texture> renderTexture = nullptr;
  if (config.sampleCount > 1) {
    TextureDescriptor textureDesc(outputTexture->width(), outputTexture->height(),
                                  outputTexture->format(), false, config.sampleCount,
                                  TextureUsage::RENDER_ATTACHMENT);
    renderTexture = gpu->createTexture(textureDesc);
    if (renderTexture == nullptr) {
      return false;
    }
  }

  auto msaaTexture = renderTexture != nullptr ? renderTexture : outputTexture;
  RenderPassDescriptor renderPassDesc(msaaTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent(),
                                      renderTexture != nullptr ? outputTexture : nullptr);
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    return false;
  }
  renderPass->setPipeline(std::move(pipeline));

  // Draw a single triangle covering the upper-left half of the output texture.
  // The diagonal edge makes MSAA anti-aliasing effects clearly visible.
  static constexpr size_t VERTEX_COUNT = 3;
  static constexpr size_t FLOATS_PER_VERTEX = 2 + 4;  // position(2) + color(4)
  static constexpr size_t VERTEX_SIZE = VERTEX_COUNT * FLOATS_PER_VERTEX * sizeof(float);
  auto vertexBuffer = gpu->createBuffer(VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }

  float r = config.outputColor.red * config.outputColor.alpha;
  float g = config.outputColor.green * config.outputColor.alpha;
  float b = config.outputColor.blue * config.outputColor.alpha;
  float a = config.outputColor.alpha;

  // Triangle: top-left -> top-right -> bottom-left (diagonal from top-right to bottom-left)
  size_t i = 0;
  vertices[i++] = -1.0f;
  vertices[i++] = 1.0f;
  vertices[i++] = r;
  vertices[i++] = g;
  vertices[i++] = b;
  vertices[i++] = a;
  vertices[i++] = 1.0f;
  vertices[i++] = 1.0f;
  vertices[i++] = r;
  vertices[i++] = g;
  vertices[i++] = b;
  vertices[i++] = a;
  vertices[i++] = -1.0f;
  vertices[i++] = -1.0f;
  vertices[i++] = r;
  vertices[i++] = g;
  vertices[i++] = b;
  vertices[i++] = a;

  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->draw(PrimitiveType::Triangles, VERTEX_COUNT);
  renderPass->end();
  return true;
}
}  // namespace tgfx
