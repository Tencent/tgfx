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
#include <memory>
#include "tgfx/gpu/GPU.h"
#include "utils/NativeShader.h"

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

// The native sources mirror the removed GLSL-translation layer: Metal and D3D12 negate the
// clip-space y, GL/Vulkan/WebGPU pass it through, and the Args uniform block resolves by name
// through ShaderModuleDescriptor::uniformSlots (slot 0 on every native backend).
static constexpr char MSL_VERTEX_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float2 inPosition [[attribute(0)]];
  float2 inOffset [[attribute(1)]];
  float4 inColor [[attribute(2)]];
};

struct Args {
  float2 viewSize;
};

struct VertexOut {
  float4 position [[position]];
  float4 vertexColor;
};

vertex VertexOut main0(VertexIn in [[stage_in]], constant Args& args [[buffer(0)]]) {
  VertexOut out;
  float2 pixelPos = in.inPosition + in.inOffset;
  float2 ndcPos = (pixelPos / args.viewSize) * 2.0 - 1.0;
  out.position = float4(ndcPos.x, -ndcPos.y, 0.0, 1.0);
  out.vertexColor = in.inColor;
  return out;
}
)";

static constexpr char MSL_FRAGMENT_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
  float4 position [[position]];
  float4 vertexColor;
};

fragment float4 main0(VertexOut in [[stage_in]]) {
  return in.vertexColor;
}
)";

static const uint32_t kInstancedGridVertexSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000033, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x000a000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x0000000d, 0x00000026,
    0x0000002e, 0x00000030, 0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00040047, 0x0000000d,
    0x0000001e, 0x00000001, 0x00030047, 0x00000012, 0x00000002, 0x00050048, 0x00000012, 0x00000000,
    0x00000023, 0x00000000, 0x00040047, 0x00000014, 0x00000021, 0x00000000, 0x00040047, 0x00000014,
    0x00000022, 0x00000000, 0x00030047, 0x00000024, 0x00000002, 0x00050048, 0x00000024, 0x00000000,
    0x0000000b, 0x00000000, 0x00050048, 0x00000024, 0x00000001, 0x0000000b, 0x00000001, 0x00050048,
    0x00000024, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x00000024, 0x00000003, 0x0000000b,
    0x00000004, 0x00040047, 0x0000002e, 0x0000001e, 0x00000000, 0x00040047, 0x00000030, 0x0000001e,
    0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x0000000a, 0x00000001,
    0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001, 0x0004003b, 0x0000000a, 0x0000000d,
    0x00000001, 0x0003001e, 0x00000012, 0x00000007, 0x00040020, 0x00000013, 0x00000002, 0x00000012,
    0x0004003b, 0x00000013, 0x00000014, 0x00000002, 0x00040015, 0x00000015, 0x00000020, 0x00000001,
    0x0004002b, 0x00000015, 0x00000016, 0x00000000, 0x00040020, 0x00000017, 0x00000002, 0x00000007,
    0x0004002b, 0x00000006, 0x0000001b, 0x40000000, 0x0004002b, 0x00000006, 0x0000001d, 0x3f800000,
    0x00040017, 0x00000020, 0x00000006, 0x00000004, 0x00040015, 0x00000021, 0x00000020, 0x00000000,
    0x0004002b, 0x00000021, 0x00000022, 0x00000001, 0x0004001c, 0x00000023, 0x00000006, 0x00000022,
    0x0006001e, 0x00000024, 0x00000020, 0x00000006, 0x00000023, 0x00000023, 0x00040020, 0x00000025,
    0x00000003, 0x00000024, 0x0004003b, 0x00000025, 0x00000026, 0x00000003, 0x0004002b, 0x00000006,
    0x00000028, 0x00000000, 0x00040020, 0x0000002c, 0x00000003, 0x00000020, 0x0004003b, 0x0000002c,
    0x0000002e, 0x00000003, 0x00040020, 0x0000002f, 0x00000001, 0x00000020, 0x0004003b, 0x0000002f,
    0x00000030, 0x00000001, 0x0005002c, 0x00000007, 0x00000032, 0x0000001d, 0x0000001d, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007,
    0x0000000c, 0x0000000b, 0x0004003d, 0x00000007, 0x0000000e, 0x0000000d, 0x00050081, 0x00000007,
    0x0000000f, 0x0000000c, 0x0000000e, 0x00050041, 0x00000017, 0x00000018, 0x00000014, 0x00000016,
    0x0004003d, 0x00000007, 0x00000019, 0x00000018, 0x00050088, 0x00000007, 0x0000001a, 0x0000000f,
    0x00000019, 0x0005008e, 0x00000007, 0x0000001c, 0x0000001a, 0x0000001b, 0x00050083, 0x00000007,
    0x0000001f, 0x0000001c, 0x00000032, 0x00050051, 0x00000006, 0x00000029, 0x0000001f, 0x00000000,
    0x00050051, 0x00000006, 0x0000002a, 0x0000001f, 0x00000001, 0x00070050, 0x00000020, 0x0000002b,
    0x00000029, 0x0000002a, 0x00000028, 0x0000001d, 0x00050041, 0x0000002c, 0x0000002d, 0x00000026,
    0x00000016, 0x0003003e, 0x0000002d, 0x0000002b, 0x0004003d, 0x00000020, 0x00000031, 0x00000030,
    0x0003003e, 0x0000002e, 0x00000031, 0x000100fd, 0x00010038,
};

static const uint32_t kInstancedGridFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000000d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040020, 0x0000000a,
    0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
    0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x000100fd, 0x00010038,

};

static constexpr char WGSL_VERTEX_SHADER[] = R"(
struct VertexIn {
  @location(0) inPosition: vec2f,
  @location(1) inOffset: vec2f,
  @location(2) inColor: vec4f,
};

struct Args {
  viewSize: vec2f,
};

@group(0) @binding(0) var<uniform> args: Args;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) vertexColor: vec4f,
};

@vertex
fn main(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  let pixelPos = in.inPosition + in.inOffset;
  let ndcPos = (pixelPos / args.viewSize) * 2.0 - 1.0;
  out.position = vec4f(ndcPos, 0.0, 1.0);
  out.vertexColor = in.inColor;
  return out;
}
)";

static constexpr char WGSL_FRAGMENT_SHADER[] = R"(
struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) vertexColor: vec4f,
};

@fragment
fn main(in: VertexOut) -> @location(0) vec4f {
  return in.vertexColor;
}
)";

static constexpr char HLSL_VERTEX_SHADER[] = R"(
cbuffer Args : register(b0) {
  float2 viewSize;
};

struct VSIn {
  float2 inPosition : TEXCOORD0;
  float2 inOffset : TEXCOORD1;
  float4 inColor : TEXCOORD2;
};

struct VSOut {
  float4 position : SV_Position;
  float4 vertexColor : TEXCOORD0;
};

VSOut main(VSIn input) {
  VSOut output;
  float2 pixelPos = input.inPosition + input.inOffset;
  float2 ndcPos = (pixelPos / viewSize) * 2.0 - 1.0;
  output.position = float4(ndcPos.x, -ndcPos.y, 0.0, 1.0);
  output.vertexColor = input.inColor;
  return output;
}
)";

static constexpr char HLSL_FRAGMENT_SHADER[] = R"(
struct PSIn {
  float4 position : SV_Position;
  float4 vertexColor : TEXCOORD0;
};

float4 main(PSIn input) : SV_Target {
  return input.vertexColor;
}
)";

static const NativeShaderStage kVertexSource = {
    VERTEX_SHADER,
    MSL_VERTEX_SHADER,
    kInstancedGridVertexSpirv,
    sizeof(kInstancedGridVertexSpirv) / sizeof(uint32_t),
    WGSL_VERTEX_SHADER,
    HLSL_VERTEX_SHADER,
    {{"Args", 0}}};

static const NativeShaderStage kFragmentSource = {
    FRAGMENT_SHADER,
    MSL_FRAGMENT_SHADER,
    kInstancedGridFragmentSpirv,
    sizeof(kInstancedGridFragmentSpirv) / sizeof(uint32_t),
    WGSL_FRAGMENT_SHADER,
    HLSL_FRAGMENT_SHADER,
    {}};

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

std::shared_ptr<InstancedGridRenderPass> InstancedGridRenderPass::Make(uint32_t rows,
                                                                       uint32_t columns) {
  return std::shared_ptr<InstancedGridRenderPass>(new InstancedGridRenderPass(rows, columns));
}

InstancedGridRenderPass::InstancedGridRenderPass(uint32_t rows, uint32_t columns)
    : rows(rows), columns(columns) {
  position = {"inPosition", VertexFormat::Float2};
  offset = {"inOffset", VertexFormat::Float2};
  color = {"inColor", VertexFormat::Float4};
}

std::shared_ptr<RenderPipeline> InstancedGridRenderPass::createPipeline(GPU* gpu) const {
  std::shared_ptr<ShaderModule> vertexShader = nullptr;
  std::shared_ptr<ShaderModule> fragmentShader = nullptr;
  if (!CreateNativeShaderModules(gpu, kVertexSource, kFragmentSource, &vertexShader,
                                 &fragmentShader)) {
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
  descriptor.layout.uniformBlocks = {{"Args", 0, ShaderVisibility::Vertex}};
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
