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
#include <memory>
#include "tgfx/gpu/GPU.h"
#include "utils/NativeShader.h"

namespace tgfx {

// Vertex shader: draws a diagonal triangle covering roughly the upper-left half of the output.
// The vertex color (including alpha) is passed through from the vertex attribute. The native
// sources reproduce the removed GLSL-translation layer's output: Metal and D3D12 negate the
// clip-space y (the old SPIRV-Cross flip), GL/Vulkan/WebGPU pass it through, so every backend's
// pixel baseline stays identical to the recorded one.
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

static constexpr char MSL_VERTEX_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float2 aPosition [[attribute(0)]];
  float4 aColor [[attribute(1)]];
};

struct VertexOut {
  float4 position [[position]];
  float4 vertexColor;
};

vertex VertexOut main0(VertexIn in [[stage_in]]) {
  VertexOut out;
  out.position = float4(in.aPosition.x, -in.aPosition.y, 0.0, 1.0);
  out.vertexColor = in.aColor;
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

static const uint32_t kMultisampleVertexSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000001f, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00000012, 0x0000001b,
    0x0000001d, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b, 0x00000000, 0x0000000b,
    0x00000000, 0x00050048, 0x0000000b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000000b,
    0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b, 0x00000003, 0x0000000b, 0x00000004,
    0x00040047, 0x00000012, 0x0000001e, 0x00000000, 0x00040047, 0x0000001b, 0x0000001e, 0x00000000,
    0x00040047, 0x0000001d, 0x0000001e, 0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
    0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001,
    0x0004001c, 0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007, 0x00000006,
    0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c,
    0x0000000d, 0x00000003, 0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e,
    0x0000000f, 0x00000000, 0x00040017, 0x00000010, 0x00000006, 0x00000002, 0x00040020, 0x00000011,
    0x00000001, 0x00000010, 0x0004003b, 0x00000011, 0x00000012, 0x00000001, 0x0004002b, 0x00000006,
    0x00000014, 0x00000000, 0x0004002b, 0x00000006, 0x00000015, 0x3f800000, 0x00040020, 0x00000019,
    0x00000003, 0x00000007, 0x0004003b, 0x00000019, 0x0000001b, 0x00000003, 0x00040020, 0x0000001c,
    0x00000001, 0x00000007, 0x0004003b, 0x0000001c, 0x0000001d, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000010, 0x00000013,
    0x00000012, 0x00050051, 0x00000006, 0x00000016, 0x00000013, 0x00000000, 0x00050051, 0x00000006,
    0x00000017, 0x00000013, 0x00000001, 0x00070050, 0x00000007, 0x00000018, 0x00000016, 0x00000017,
    0x00000014, 0x00000015, 0x00050041, 0x00000019, 0x0000001a, 0x0000000d, 0x0000000f, 0x0003003e,
    0x0000001a, 0x00000018, 0x0004003d, 0x00000007, 0x0000001e, 0x0000001d, 0x0003003e, 0x0000001b,
    0x0000001e, 0x000100fd, 0x00010038,
};

static const uint32_t kMultisampleFragmentSpirv[] = {
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
  @location(0) aPosition: vec2f,
  @location(1) aColor: vec4f,
};

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) vertexColor: vec4f,
};

@vertex
fn main(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  out.position = vec4f(in.aPosition, 0.0, 1.0);
  out.vertexColor = in.aColor;
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
struct VSIn {
  float2 aPosition : TEXCOORD0;
  float4 aColor : TEXCOORD1;
};

struct VSOut {
  float4 position : SV_Position;
  float4 vertexColor : TEXCOORD0;
};

VSOut main(VSIn input) {
  VSOut output;
  output.position = float4(input.aPosition.x, -input.aPosition.y, 0.0, 1.0);
  output.vertexColor = input.aColor;
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

static const NativeShaderStage kVertexSource = {VERTEX_SHADER,
                                                MSL_VERTEX_SHADER,
                                                kMultisampleVertexSpirv,
                                                sizeof(kMultisampleVertexSpirv) / sizeof(uint32_t),
                                                WGSL_VERTEX_SHADER,
                                                HLSL_VERTEX_SHADER,
                                                {}};

static const NativeShaderStage kFragmentSource = {
    FRAGMENT_SHADER,
    MSL_FRAGMENT_SHADER,
    kMultisampleFragmentSpirv,
    sizeof(kMultisampleFragmentSpirv) / sizeof(uint32_t),
    WGSL_FRAGMENT_SHADER,
    HLSL_FRAGMENT_SHADER,
    {}};

std::shared_ptr<MultisampleTestEffect> MultisampleTestEffect::Make(
    const MultisampleConfig& config) {
  return std::shared_ptr<MultisampleTestEffect>(new MultisampleTestEffect(config));
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
  std::shared_ptr<ShaderModule> vertexShader = nullptr;
  std::shared_ptr<ShaderModule> fragmentShader = nullptr;
  if (!CreateNativeShaderModules(gpu, kVertexSource, kFragmentSource, &vertexShader,
                                 &fragmentShader)) {
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
                                   std::shared_ptr<Texture> outputTexture, const Point&) const {
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
