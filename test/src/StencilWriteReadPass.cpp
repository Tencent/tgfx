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

#include "StencilWriteReadPass.h"
#include <cstring>
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Vertex shader: emits an oversized triangle that fully covers the NDC viewport.
static constexpr char VERTEX_SHADER[] = R"(
        in vec2 inPosition;
        void main() {
            gl_Position = vec4(inPosition, 0.0, 1.0);
        }
    )";

// Fragment shader: outputs a constant color supplied via a uniform block.
static constexpr char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        layout(std140) uniform Args {
            vec4 uColor;
        };
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = uColor;
        }
    )";

struct VertexData {
  float x;
  float y;
};

struct UniformData {
  float r;
  float g;
  float b;
  float a;
};

// Reference value used by both the stencil-write pass (replaces stencil with this value) and the
// stencil-read pass (passes only when stencil equals this value).
static constexpr uint32_t STENCIL_REFERENCE = 1;

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + codeSnippet;
  }
  return std::string("#version 300 es\n\n") + codeSnippet;
}

std::shared_ptr<StencilWriteReadPass> StencilWriteReadPass::Make() {
  return std::shared_ptr<StencilWriteReadPass>(new StencilWriteReadPass());
}

std::shared_ptr<RenderPipeline> StencilWriteReadPass::createPipeline(GPU* gpu,
                                                                     bool stencilWrite) const {
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
  VertexBufferLayout vertexLayout({{"inPosition", VertexFormat::Float2}});
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  PipelineColorAttachment colorAttachment = {};
  if (stencilWrite) {
    // The write pass intentionally disables color writes so its only observable effect is the
    // mutation of the stencil buffer.
    colorAttachment.colorWriteMask = 0;
  }
  descriptor.fragment.colorAttachments.push_back(colorAttachment);
  descriptor.layout.uniformBlocks = {{"Args", 0, ShaderVisibility::Fragment}};

  if (stencilWrite) {
    descriptor.depthStencil.stencilFront.compare = CompareFunction::Always;
    descriptor.depthStencil.stencilFront.passOp = StencilOperation::Replace;
    descriptor.depthStencil.stencilBack.compare = CompareFunction::Always;
    descriptor.depthStencil.stencilBack.passOp = StencilOperation::Replace;
  } else {
    descriptor.depthStencil.stencilFront.compare = CompareFunction::Equal;
    descriptor.depthStencil.stencilBack.compare = CompareFunction::Equal;
  }
  descriptor.depthStencil.format = PixelFormat::DEPTH24_STENCIL8;
  return gpu->createRenderPipeline(descriptor);
}

static std::shared_ptr<GPUBuffer> MakeFullscreenTriangle(GPU* gpu) {
  // A single oversized triangle that covers the entire NDC viewport.
  static const VertexData VERTICES[] = {{-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f}};
  auto buffer = gpu->createBuffer(sizeof(VERTICES), GPUBufferUsage::VERTEX);
  if (buffer == nullptr) {
    return nullptr;
  }
  auto data = buffer->map();
  if (data == nullptr) {
    return nullptr;
  }
  memcpy(data, VERTICES, sizeof(VERTICES));
  buffer->unmap();
  return buffer;
}

static std::shared_ptr<GPUBuffer> MakeColorUniform(GPU* gpu, const Color& color) {
  // The std140 layout requires alignment to 16 bytes.
  auto buffer = gpu->createBuffer(sizeof(UniformData), GPUBufferUsage::UNIFORM);
  if (buffer == nullptr) {
    return nullptr;
  }
  auto data = static_cast<UniformData*>(buffer->map());
  if (data == nullptr) {
    return nullptr;
  }
  data->r = color.red * color.alpha;
  data->g = color.green * color.alpha;
  data->b = color.blue * color.alpha;
  data->a = color.alpha;
  buffer->unmap();
  return buffer;
}

bool StencilWriteReadPass::onDraw(CommandEncoder* encoder,
                                  std::shared_ptr<Texture> renderTexture) const {
  auto gpu = encoder->gpu();
  if (gpu == nullptr || renderTexture == nullptr) {
    return false;
  }

  auto writePipeline = createPipeline(gpu, /*stencilWrite=*/true);
  auto readPipeline = createPipeline(gpu, /*stencilWrite=*/false);
  if (writePipeline == nullptr || readPipeline == nullptr) {
    return false;
  }

  TextureDescriptor stencilDesc(renderTexture->width(), renderTexture->height(),
                                PixelFormat::DEPTH24_STENCIL8, false, 1,
                                TextureUsage::RENDER_ATTACHMENT);
  auto stencilTexture = gpu->createTexture(stencilDesc);
  if (stencilTexture == nullptr) {
    return false;
  }

  auto vertexBuffer = MakeFullscreenTriangle(gpu);
  auto writeColor = MakeColorUniform(gpu, Color::White());
  auto readColor = MakeColorUniform(gpu, Color::Red());
  if (vertexBuffer == nullptr || writeColor == nullptr || readColor == nullptr) {
    return false;
  }

  // Pass 1: stencil-only write. Replace the stencil buffer with STENCIL_REFERENCE everywhere the
  // fullscreen triangle is rasterized. Color writes are disabled by the pipeline.
  RenderPassDescriptor writePassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                     PMColor::Transparent());
  writePassDesc.depthStencilAttachment =
      DepthStencilAttachment(stencilTexture, LoadAction::Clear, StoreAction::Store,
                             /*depthClearValue=*/1.0f, /*depthReadOnly=*/false,
                             /*stencilClearValue=*/0, /*stencilReadOnly=*/false);
  auto writePass = encoder->beginRenderPass(writePassDesc);
  if (writePass == nullptr) {
    return false;
  }
  writePass->setPipeline(std::move(writePipeline));
  writePass->setStencilReference(STENCIL_REFERENCE);
  writePass->setVertexBuffer(0, vertexBuffer);
  writePass->setUniformBuffer(0, writeColor, 0, writeColor->size());
  writePass->draw(PrimitiveType::Triangles, 3);
  writePass->end();

  // Pass 2: clear color to black and draw red gated by the stencil value written in Pass 1.
  RenderPassDescriptor readPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                    Color::Black().premultiply());
  readPassDesc.depthStencilAttachment =
      DepthStencilAttachment(stencilTexture, LoadAction::Load, StoreAction::DontCare,
                             /*depthClearValue=*/1.0f, /*depthReadOnly=*/false,
                             /*stencilClearValue=*/0, /*stencilReadOnly=*/true);
  auto readPass = encoder->beginRenderPass(readPassDesc);
  if (readPass == nullptr) {
    return false;
  }
  readPass->setPipeline(std::move(readPipeline));
  readPass->setStencilReference(STENCIL_REFERENCE);
  readPass->setVertexBuffer(0, vertexBuffer);
  readPass->setUniformBuffer(0, readColor, 0, readColor->size());
  readPass->draw(PrimitiveType::Triangles, 3);
  readPass->end();
  return true;
}

}  // namespace tgfx
