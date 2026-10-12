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

#include "ArrayVaryingEffect.h"
#include <cstring>
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// The vertex shader declares an array-typed varying (colors[2]) followed by a trailing scalar
// varying (lastColor). This exercises the ShaderCompiler location-assignment pass for custom GLSL:
// the array must be recognised and reserved for its full element count, so the trailing varying
// lands after the array instead of overlapping its slots.
static constexpr char VERTEX_SHADER[] = R"(
        in vec2 aPosition;
        out vec4 colors[2];
        out vec4 lastColor;
        void main() {
            gl_Position = vec4(aPosition, 0.0, 1.0);
            colors[0] = vec4(1.0, 0.0, 0.0, 1.0);
            colors[1] = vec4(0.0, 1.0, 0.0, 1.0);
            lastColor = vec4(0.0, 0.0, 1.0, 1.0);
        }
    )";

// The fragment shader reads the array varying and the trailing varying and outputs the trailing
// varying's constant blue. If the location counter only advanced by one per declaration, the
// trailing varying would collide with colors[1] and SPIR-V compilation would fail.
static constexpr char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec4 colors[2];
        in vec4 lastColor;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = lastColor + colors[0] * 0.0 + colors[1] * 0.0;
        }
    )";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

std::shared_ptr<ArrayVaryingEffect> ArrayVaryingEffect::Make() {
  return std::shared_ptr<ArrayVaryingEffect>(new ArrayVaryingEffect());
}

Rect ArrayVaryingEffect::filterBounds(const Rect& srcRect, MapDirection mapDirection) const {
  if (mapDirection == MapDirection::Reverse) {
    const auto largeSize = static_cast<float>(1 << 29);
    return Rect::MakeLTRB(-largeSize, -largeSize, largeSize, largeSize);
  }
  return srcRect;
}

std::shared_ptr<RenderPipeline> ArrayVaryingEffect::createPipeline(GPU* gpu) const {
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
  VertexBufferLayout vertexLayout({{"aPosition", VertexFormat::Float2}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  return gpu->createRenderPipeline(descriptor);
}

bool ArrayVaryingEffect::onDraw(CommandEncoder* encoder,
                                const std::vector<std::shared_ptr<Texture>>&,
                                std::shared_ptr<Texture> outputTexture, const Point&) const {
  auto gpu = encoder->gpu();
  auto pipeline = createPipeline(gpu);
  if (pipeline == nullptr) {
    return false;
  }

  RenderPassDescriptor renderPassDesc(outputTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent(), nullptr);
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    return false;
  }
  renderPass->setPipeline(std::move(pipeline));

  // A quad covering the central half of NDC in both axes. On a 200x200 surface this yields a
  // centred 100x100 rectangle with a 50-pixel margin on every side.
  static constexpr size_t VERTEX_COUNT = 4;
  static constexpr size_t FLOATS_PER_VERTEX = 2;
  static constexpr size_t VERTEX_SIZE = VERTEX_COUNT * FLOATS_PER_VERTEX * sizeof(float);
  auto vertexBuffer = gpu->createBuffer(VERTEX_SIZE, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return false;
  }
  static constexpr float CENTER_QUAD[VERTEX_COUNT * FLOATS_PER_VERTEX] = {
      -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f,
  };
  memcpy(vertices, CENTER_QUAD, sizeof(CENTER_QUAD));
  vertexBuffer->unmap();
  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->draw(PrimitiveType::TriangleStrip, VERTEX_COUNT);
  renderPass->end();
  return true;
}

}  // namespace tgfx
