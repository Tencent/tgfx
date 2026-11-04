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
#include <memory>
#include "tgfx/gpu/GPU.h"
#include "utils/NativeShader.h"

namespace tgfx {

// The vertex shader declares an array-typed varying (colors[2]) followed by a trailing scalar
// varying (lastColor). This exercises the backend-native location-assignment contracts: the
// array must reserve its full element count, so the trailing varying lands after the array
// instead of overlapping its slots. The native sources mirror the removed GLSL-translation
// layer (Metal/D3D12 negate clip-space y; GL/Vulkan/WebGPU pass it through).
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
// varying's constant blue. If the location assignment only advanced by one per declaration, the
// trailing varying would collide with colors[1] and the SPIR-V toolchain would reject the
// module.
static constexpr char FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec4 colors[2];
        in vec4 lastColor;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = lastColor + colors[0] * 0.0 + colors[1] * 0.0;
        }
    )";

// Metal forbids array members in a [[stage_in]] struct, so the MSL variant flattens the
// colors[2] array into two scalar members. stage_in matches by member layout (not by name or
// location), and the members carry the same constants, so the interpolated output is identical.
static constexpr char MSL_VERTEX_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float2 aPosition [[attribute(0)]];
};

struct VertexOut {
  float4 position [[position]];
  float4 color0;
  float4 color1;
  float4 lastColor;
};

vertex VertexOut main0(VertexIn in [[stage_in]]) {
  VertexOut out;
  out.position = float4(in.aPosition.x, -in.aPosition.y, 0.0, 1.0);
  out.color0 = float4(1.0, 0.0, 0.0, 1.0);
  out.color1 = float4(0.0, 1.0, 0.0, 1.0);
  out.lastColor = float4(0.0, 0.0, 1.0, 1.0);
  return out;
}
)";

static constexpr char MSL_FRAGMENT_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
  float4 position [[position]];
  float4 color0;
  float4 color1;
  float4 lastColor;
};

fragment float4 main0(VertexOut in [[stage_in]]) {
  return in.lastColor + in.color0 * 0.0 + in.color1 * 0.0;
}
)";

static const uint32_t kArrayVaryingVertexSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000026, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00000012, 0x0000001e,
    0x00000024, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b, 0x00000000, 0x0000000b,
    0x00000000, 0x00050048, 0x0000000b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000000b,
    0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b, 0x00000003, 0x0000000b, 0x00000004,
    0x00040047, 0x00000012, 0x0000001e, 0x00000000, 0x00040047, 0x0000001e, 0x0000001e, 0x00000000,
    0x00040047, 0x00000024, 0x0000001e, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
    0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001,
    0x0004001c, 0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007, 0x00000006,
    0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c,
    0x0000000d, 0x00000003, 0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e,
    0x0000000f, 0x00000000, 0x00040017, 0x00000010, 0x00000006, 0x00000002, 0x00040020, 0x00000011,
    0x00000001, 0x00000010, 0x0004003b, 0x00000011, 0x00000012, 0x00000001, 0x0004002b, 0x00000006,
    0x00000014, 0x00000000, 0x0004002b, 0x00000006, 0x00000015, 0x3f800000, 0x00040020, 0x00000019,
    0x00000003, 0x00000007, 0x0004002b, 0x00000008, 0x0000001b, 0x00000002, 0x0004001c, 0x0000001c,
    0x00000007, 0x0000001b, 0x00040020, 0x0000001d, 0x00000003, 0x0000001c, 0x0004003b, 0x0000001d,
    0x0000001e, 0x00000003, 0x0007002c, 0x00000007, 0x0000001f, 0x00000015, 0x00000014, 0x00000014,
    0x00000015, 0x0004002b, 0x0000000e, 0x00000021, 0x00000001, 0x0007002c, 0x00000007, 0x00000022,
    0x00000014, 0x00000015, 0x00000014, 0x00000015, 0x0004003b, 0x00000019, 0x00000024, 0x00000003,
    0x0007002c, 0x00000007, 0x00000025, 0x00000014, 0x00000014, 0x00000015, 0x00000015, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000010,
    0x00000013, 0x00000012, 0x00050051, 0x00000006, 0x00000016, 0x00000013, 0x00000000, 0x00050051,
    0x00000006, 0x00000017, 0x00000013, 0x00000001, 0x00070050, 0x00000007, 0x00000018, 0x00000016,
    0x00000017, 0x00000014, 0x00000015, 0x00050041, 0x00000019, 0x0000001a, 0x0000000d, 0x0000000f,
    0x0003003e, 0x0000001a, 0x00000018, 0x00050041, 0x00000019, 0x00000020, 0x0000001e, 0x0000000f,
    0x0003003e, 0x00000020, 0x0000001f, 0x00050041, 0x00000019, 0x00000023, 0x0000001e, 0x00000021,
    0x0003003e, 0x00000023, 0x00000022, 0x0003003e, 0x00000024, 0x00000025, 0x000100fd, 0x00010038,
};

static const uint32_t kArrayVaryingFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000000d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040020, 0x0000000a,
    0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
    0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x000100fd, 0x00010038,

};

static constexpr char WGSL_VERTEX_SHADER[] = R"(
struct VertexIn {
  @location(0) aPosition: vec2f,
};

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) colors: array<vec4f, 2>,
  @location(2) lastColor: vec4f,
};

@vertex
fn main(in: VertexIn) -> VertexOut {
  var out: VertexOut;
  out.position = vec4f(in.aPosition, 0.0, 1.0);
  out.colors[0] = vec4f(1.0, 0.0, 0.0, 1.0);
  out.colors[1] = vec4f(0.0, 1.0, 0.0, 1.0);
  out.lastColor = vec4f(0.0, 0.0, 1.0, 1.0);
  return out;
}
)";

static constexpr char WGSL_FRAGMENT_SHADER[] = R"(
struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) colors: array<vec4f, 2>,
  @location(2) lastColor: vec4f,
};

@fragment
fn main(in: VertexOut) -> @location(0) vec4f {
  return in.lastColor + in.colors[0] * 0.0 + in.colors[1] * 0.0;
}
)";

static constexpr char HLSL_VERTEX_SHADER[] = R"(
struct VSIn {
  float2 aPosition : TEXCOORD0;
};

struct VSOut {
  float4 position : SV_Position;
  float4 colors[2] : TEXCOORD0;
  float4 lastColor : TEXCOORD2;
};

VSOut main(VSIn input) {
  VSOut output;
  output.position = float4(input.aPosition.x, -input.aPosition.y, 0.0, 1.0);
  output.colors[0] = float4(1.0, 0.0, 0.0, 1.0);
  output.colors[1] = float4(0.0, 1.0, 0.0, 1.0);
  output.lastColor = float4(0.0, 0.0, 1.0, 1.0);
  return output;
}
)";

static constexpr char HLSL_FRAGMENT_SHADER[] = R"(
struct PSIn {
  float4 position : SV_Position;
  float4 colors[2] : TEXCOORD0;
  float4 lastColor : TEXCOORD2;
};

float4 main(PSIn input) : SV_Target {
  return input.lastColor + input.colors[0] * 0.0 + input.colors[1] * 0.0;
}
)";

static const NativeShaderStage kVertexSource = {VERTEX_SHADER,
                                                MSL_VERTEX_SHADER,
                                                kArrayVaryingVertexSpirv,
                                                sizeof(kArrayVaryingVertexSpirv) / sizeof(uint32_t),
                                                WGSL_VERTEX_SHADER,
                                                HLSL_VERTEX_SHADER,
                                                {}};

static const NativeShaderStage kFragmentSource = {
    FRAGMENT_SHADER,
    MSL_FRAGMENT_SHADER,
    kArrayVaryingFragmentSpirv,
    sizeof(kArrayVaryingFragmentSpirv) / sizeof(uint32_t),
    WGSL_FRAGMENT_SHADER,
    HLSL_FRAGMENT_SHADER,
    {}};

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
  std::shared_ptr<ShaderModule> vertexShader = nullptr;
  std::shared_ptr<ShaderModule> fragmentShader = nullptr;
  if (!CreateNativeShaderModules(gpu, kVertexSource, kFragmentSource, &vertexShader,
                                 &fragmentShader)) {
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
