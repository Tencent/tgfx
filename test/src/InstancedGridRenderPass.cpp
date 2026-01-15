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

#include "InstancedGridRenderPass.h"
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {

static constexpr char VERTEX_SHADER[] = R"(
        in vec2 inPosition;
        in vec2 inOffset;
        in vec4 inColor;

        layout(std140) uniform Args {
            vec2 viewSize;
        };
        out vec4 vertexColor;
        void main() {
            vec2 pixelPos = inPosition + inOffset;
            vec2 ndcPos = (pixelPos / viewSize) * 2.0 - 1.0;
            gl_Position = vec4(ndcPos, 0.0, 1.0);
            vertexColor = inColor;
        }
    )";

static constexpr char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec4 vertexColor;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = vertexColor;
        }
    )";

struct VertexData {
  float x;
  float y;
};

struct InstanceData {
  float tx;
  float ty;
  float r;
  float g;
  float b;
  float a;
};

struct UniformData {
  float width;
  float height;
};

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

std::shared_ptr<InstancedGridRenderPass> InstancedGridRenderPass::Make(uint32_t rows,
                                                                       uint32_t columns) {
  return std::make_shared<InstancedGridRenderPass>(rows, columns);
}

InstancedGridRenderPass::InstancedGridRenderPass(uint32_t rows, uint32_t columns)
    : rows(rows), columns(columns) {
  position = {"inPosition", VertexFormat::Float2};
  offset = {"inOffset", VertexFormat::Float2};
  color = {"inColor", VertexFormat::Float4};
}

std::shared_ptr<RenderPipeline> InstancedGridRenderPass::createPipeline(GPU* gpu) const {
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
  // Separate vertex and instance attributes
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  VertexBufferLayout instanceLayout({offset, color}, VertexStepMode::Instance);
  descriptor.vertex.bufferLayouts = {vertexLayout, instanceLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  tgfx::PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = true;
  colorAttachment.srcColorBlendFactor = tgfx::BlendFactor::One;
  colorAttachment.dstColorBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
  colorAttachment.srcAlphaBlendFactor = tgfx::BlendFactor::One;
  colorAttachment.dstAlphaBlendFactor = tgfx::BlendFactor::OneMinusSrcAlpha;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);
  descriptor.layout.uniformBlocks = {{"Args", 0}};
  return gpu->createRenderPipeline(descriptor);
}

bool InstancedGridRenderPass::onDraw(CommandEncoder* encoder,
                                     std::shared_ptr<Texture> renderTexture) const {
  auto gpu = encoder->gpu();
  if (gpu == nullptr) {
    return false;
  }

  if (renderTexture == nullptr) {
    return false;
  }

  auto pipeline = createPipeline(gpu);
  if (pipeline == nullptr) {
    return false;
  }

  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    return false;
  }
  renderPass->setPipeline(std::move(pipeline));

  auto textureWidth = renderTexture->width();
  auto textureHeight = renderTexture->height();

  VertexData quadVertices[] = {
      {0.0f, GRID_SIZE},       // bottom-left
      {GRID_SIZE, GRID_SIZE},  // bottom-right
      {0.0f, 0.0f},            // top-left
      {GRID_SIZE, 0.0f}        // top-right
  };

  // The std140 layout requires alignment to 16 bytes.
  auto uniformBuffer = gpu->createBuffer(16, GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    return false;
  }
  auto uniformData = static_cast<UniformData*>(uniformBuffer->map());
  if (uniformData == nullptr) {
    return false;
  }
  uniformData->width = static_cast<float>(textureWidth);
  uniformData->height = static_cast<float>(textureHeight);
  uniformBuffer->unmap();

  auto vertexBufferSize = sizeof(quadVertices);
  auto vertexBuffer = gpu->createBuffer(vertexBufferSize, GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return false;
  }
  auto vertexData = static_cast<VertexData*>(vertexBuffer->map());
  if (vertexData == nullptr) {
    return false;
  }
  memcpy(vertexData, quadVertices, vertexBufferSize);
  vertexBuffer->unmap();

  auto gridCount = rows * columns;
  auto instanceBufferSize = sizeof(InstanceData) * gridCount;
  auto instanceBuffer = gpu->createBuffer(instanceBufferSize, GPUBufferUsage::VERTEX);
  if (instanceBuffer == nullptr) {
    return false;
  }
  auto instanceData = static_cast<InstanceData*>(instanceBuffer->map());
  if (instanceData == nullptr) {
    return false;
  }
  constexpr float offset = (GRID_SIZE + GRID_SPACING);
  for (uint32_t row = 0; row < rows; row++) {
    for (uint32_t col = 0; col < columns; col++) {
      instanceData->tx = offset * static_cast<float>(col);
      instanceData->ty = offset * static_cast<float>(row);
      instanceData->r = static_cast<float>(row) / static_cast<float>(rows);
      instanceData->g = static_cast<float>(col) / static_cast<float>(columns);
      instanceData->b = 0.0f;
      instanceData->a = 1.0f;
      instanceData++;
    }
  }
  instanceBuffer->unmap();

  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformBuffer->size());
  renderPass->setVertexBuffer(1, instanceBuffer);
  renderPass->draw(PrimitiveType::TriangleStrip, 4, gridCount);
  renderPass->end();
  return true;
}

}  // namespace tgfx
