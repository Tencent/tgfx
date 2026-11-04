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

#include "StencilMaskRenderPass.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "tgfx/gpu/GPU.h"
#include "utils/NativeShader.h"

namespace tgfx {

// Both passes share a single vertex layout — one Float2 position per vertex in normalized
// device coordinates. Pass 1 feeds a triangle fan that approximates a centred disc; pass 2
// feeds two triangles covering the entire viewport. The native variants mirror the removed
// GLSL-translation layer: Metal and D3D12 negate the clip-space y, GL/Vulkan/WebGPU pass it
// through.
static constexpr char VERTEX_SHADER[] = R"(
        in vec2 inPosition;
        void main() {
            gl_Position = vec4(inPosition, 0.0, 1.0);
        }
    )";

// Pass 1's fragment shader is irrelevant — colourWriteMask is zero — but the shader still
// has to compile and produce a value. Output black; the pipeline drops it before it reaches
// the colour attachment.
static constexpr char MASK_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
    )";

// Pass 2's fragment shader emits the brush colour supplied via uniform. The std140 layout
// requires 16-byte alignment, which is exactly the size of a vec4.
static constexpr char COVER_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        layout(std140) uniform Args {
            vec4 coverColor;
        };
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = coverColor;
        }
    )";

static constexpr char MSL_VERTEX_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float2 inPosition [[attribute(0)]];
};

vertex float4 main0(VertexIn in [[stage_in]]) {
  return float4(in.inPosition.x, -in.inPosition.y, 0.0, 1.0);
}
)";

static constexpr char MSL_MASK_FRAGMENT_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

fragment float4 main0() {
  return float4(0.0, 0.0, 0.0, 1.0);
}
)";

static constexpr char MSL_COVER_FRAGMENT_SHADER[] = R"(
#include <metal_stdlib>
using namespace metal;

struct Args {
  float4 coverColor;
};

fragment float4 main0(constant Args& args [[buffer(0)]]) {
  return args.coverColor;
}
)";

static const uint32_t kStencilVertexSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000001b, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x00000012, 0x00030047,
    0x0000000b, 0x00000002, 0x00050048, 0x0000000b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x0000000b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000000b, 0x00000002, 0x0000000b,
    0x00000003, 0x00050048, 0x0000000b, 0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x00000012,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008,
    0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001, 0x0004001c, 0x0000000a,
    0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007, 0x00000006, 0x0000000a, 0x0000000a,
    0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000003,
    0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000000,
    0x00040017, 0x00000010, 0x00000006, 0x00000002, 0x00040020, 0x00000011, 0x00000001, 0x00000010,
    0x0004003b, 0x00000011, 0x00000012, 0x00000001, 0x0004002b, 0x00000006, 0x00000014, 0x00000000,
    0x0004002b, 0x00000006, 0x00000015, 0x3f800000, 0x00040020, 0x00000019, 0x00000003, 0x00000007,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x00000010, 0x00000013, 0x00000012, 0x00050051, 0x00000006, 0x00000016, 0x00000013, 0x00000000,
    0x00050051, 0x00000006, 0x00000017, 0x00000013, 0x00000001, 0x00070050, 0x00000007, 0x00000018,
    0x00000016, 0x00000017, 0x00000014, 0x00000015, 0x00050041, 0x00000019, 0x0000001a, 0x0000000d,
    0x0000000f, 0x0003003e, 0x0000001a, 0x00000018, 0x000100fd, 0x00010038,
};

static const uint32_t kStencilMaskFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000000d, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0006000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x0004002b, 0x00000006, 0x0000000a, 0x00000000, 0x0004002b, 0x00000006, 0x0000000b,
    0x3f800000, 0x0007002c, 0x00000007, 0x0000000c, 0x0000000a, 0x0000000a, 0x0000000a, 0x0000000b,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0003003e,
    0x00000009, 0x0000000c, 0x000100fd, 0x00010038,
};

static const uint32_t kStencilCoverFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x00000012, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0006000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00030047, 0x0000000a, 0x00000002,
    0x00050048, 0x0000000a, 0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x0000000c, 0x00000021,
    0x00000000, 0x00040047, 0x0000000c, 0x00000022, 0x00000001, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x0003001e, 0x0000000a, 0x00000007, 0x00040020, 0x0000000b, 0x00000002, 0x0000000a,
    0x0004003b, 0x0000000b, 0x0000000c, 0x00000002, 0x00040015, 0x0000000d, 0x00000020, 0x00000001,
    0x0004002b, 0x0000000d, 0x0000000e, 0x00000000, 0x00040020, 0x0000000f, 0x00000002, 0x00000007,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041,
    0x0000000f, 0x00000010, 0x0000000c, 0x0000000e, 0x0004003d, 0x00000007, 0x00000011, 0x00000010,
    0x0003003e, 0x00000009, 0x00000011, 0x000100fd, 0x00010038,
};

static constexpr char WGSL_VERTEX_SHADER[] = R"(
struct VertexIn {
  @location(0) inPosition: vec2f,
};

@vertex
fn main(in: VertexIn) -> @builtin(position) vec4f {
  return vec4f(in.inPosition, 0.0, 1.0);
}
)";

static constexpr char WGSL_MASK_FRAGMENT_SHADER[] = R"(
@fragment
fn main() -> @location(0) vec4f {
  return vec4f(0.0, 0.0, 0.0, 1.0);
}
)";

static constexpr char WGSL_COVER_FRAGMENT_SHADER[] = R"(
struct Args {
  coverColor: vec4f,
};

@group(1) @binding(0) var<uniform> args: Args;

@fragment
fn main() -> @location(0) vec4f {
  return args.coverColor;
}
)";

static constexpr char HLSL_VERTEX_SHADER[] = R"(
struct VSIn {
  float2 inPosition : TEXCOORD0;
};

float4 main(VSIn input) : SV_Position {
  return float4(input.inPosition.x, -input.inPosition.y, 0.0, 1.0);
}
)";

static constexpr char HLSL_MASK_FRAGMENT_SHADER[] = R"(
float4 main() : SV_Target {
  return float4(0.0, 0.0, 0.0, 1.0);
}
)";

static constexpr char HLSL_COVER_FRAGMENT_SHADER[] = R"(
cbuffer Args : register(b0) {
  float4 coverColor;
};

float4 main() : SV_Target {
  return coverColor;
}
)";

static const NativeShaderStage kVertexSource = {VERTEX_SHADER,
                                                MSL_VERTEX_SHADER,
                                                kStencilVertexSpirv,
                                                sizeof(kStencilVertexSpirv) / sizeof(uint32_t),
                                                WGSL_VERTEX_SHADER,
                                                HLSL_VERTEX_SHADER,
                                                {}};

static const NativeShaderStage kMaskFragmentSource = {
    MASK_FRAGMENT_SHADER,
    MSL_MASK_FRAGMENT_SHADER,
    kStencilMaskFragmentSpirv,
    sizeof(kStencilMaskFragmentSpirv) / sizeof(uint32_t),
    WGSL_MASK_FRAGMENT_SHADER,
    HLSL_MASK_FRAGMENT_SHADER,
    {}};

static const NativeShaderStage kCoverFragmentSource = {
    COVER_FRAGMENT_SHADER,
    MSL_COVER_FRAGMENT_SHADER,
    kStencilCoverFragmentSpirv,
    sizeof(kStencilCoverFragmentSpirv) / sizeof(uint32_t),
    WGSL_COVER_FRAGMENT_SHADER,
    HLSL_COVER_FRAGMENT_SHADER,
    {{"Args", 0}}};

// CIRCLE_SEGMENTS is the number of triangles in the fan that approximates the mask disc.
// 64 is plenty for the small render targets used by the tests; the disc edge is then well
// within one pixel of a true circle on a 200x200 surface.
static constexpr int CIRCLE_SEGMENTS = 64;

// Disc radius in normalized device coordinates. 0.6 of half-width keeps the disc comfortably
// inside the viewport while leaving a clear margin for the "outside the mask" sample point.
static constexpr float CIRCLE_RADIUS_NDC = 0.6f;

struct VertexData {
  float x;
  float y;
};

// Pair of compiled shader modules; either pointer being null means compilation failed and
// the caller must bail out.
struct CompiledShaders {
  std::shared_ptr<ShaderModule> vertex;
  std::shared_ptr<ShaderModule> fragment;
};

static CompiledShaders CompileShaders(GPU* gpu, const NativeShaderStage& fragmentSource) {
  CompiledShaders shaders = {};
  if (!CreateNativeShaderModules(gpu, kVertexSource, fragmentSource, &shaders.vertex,
                                 &shaders.fragment)) {
    return {};
  }
  return shaders;
}

std::shared_ptr<StencilMaskRenderPass> StencilMaskRenderPass::Make(PMColor coverColor) {
  return std::shared_ptr<StencilMaskRenderPass>(new StencilMaskRenderPass(coverColor));
}

StencilMaskRenderPass::StencilMaskRenderPass(PMColor color)
    : coverColor(color), position{"inPosition", VertexFormat::Float2} {
}

// Builds a render pipeline that writes the stencil buffer without touching colour. The
// `compare=Always + passOp=Replace` pattern is the canonical "stamp" stencil configuration —
// previously the GL stencil-state factory dropped this case via a `compare==Always` shortcut,
// so reaching this code path end-to-end exercises the fix that A introduces.
std::shared_ptr<RenderPipeline> StencilMaskRenderPass::createMaskPipeline(GPU* gpu) const {
  auto shaders = CompileShaders(gpu, kMaskFragmentSource);
  if (shaders.vertex == nullptr || shaders.fragment == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = shaders.vertex;
  descriptor.fragment.module = shaders.fragment;
  PipelineColorAttachment colorAttachment = {};
  // Colour is irrelevant for the mask pass — drop every channel so the colour attachment is
  // untouched. ProgramInfo forwards this mask all the way through to the
  // pipeline descriptor; the test fails (cover pass would draw on top of pass-1 colour) if
  // the plumbing breaks.
  colorAttachment.colorWriteMask = 0;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);
  // Stencil: write 1 wherever the disc fragment shader runs. compare=Always + passOp=Replace
  // is exactly the configuration the stencil-and-cover stencil pass needs and the
  // configuration GLRenderPipeline used to silently discard. Pin every stencil-related field
  // explicitly: this fixture exists to lock down the GLRenderPipeline stencil-state factory,
  // so any future change to defaults must surface here as a test break rather than a silent
  // behavioural drift. The runtime ref value (1) is bound separately via setStencilReference
  // and pairs with the cover pass's compare=Equal.
  descriptor.depthStencil.format = PixelFormat::DEPTH24_STENCIL8;
  descriptor.depthStencil.stencilFront.compare = CompareFunction::Always;
  descriptor.depthStencil.stencilFront.passOp = StencilOperation::Replace;
  descriptor.depthStencil.stencilFront.failOp = StencilOperation::Keep;
  descriptor.depthStencil.stencilFront.depthFailOp = StencilOperation::Keep;
  descriptor.depthStencil.stencilReadMask = 0xFFFFFFFF;
  descriptor.depthStencil.stencilWriteMask = 0xFFFFFFFF;
  descriptor.depthStencil.stencilBack = descriptor.depthStencil.stencilFront;
  return gpu->createRenderPipeline(descriptor);
}

// Builds the cover-pass pipeline. Stencil test is `compare=Equal + ref=1` so only fragments
// covered by pass 1 survive; passOp=Keep so the cover pass does not modify the stencil any
// further (in case the same buffer is re-used elsewhere).
std::shared_ptr<RenderPipeline> StencilMaskRenderPass::createCoverPipeline(GPU* gpu) const {
  auto shaders = CompileShaders(gpu, kCoverFragmentSource);
  if (shaders.vertex == nullptr || shaders.fragment == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = shaders.vertex;
  descriptor.fragment.module = shaders.fragment;
  PipelineColorAttachment colorAttachment = {};
  // Default ColorWriteMask::All. Two pipelines that share shaders but differ in colour write
  // mask must hash to different program-cache entries — this is what ProgramInfo's cache key
  // mixing for colorWriteMask guarantees.
  descriptor.fragment.colorAttachments.push_back(colorAttachment);
  descriptor.depthStencil.format = PixelFormat::DEPTH24_STENCIL8;
  descriptor.depthStencil.stencilFront.compare = CompareFunction::Equal;
  descriptor.depthStencil.stencilFront.passOp = StencilOperation::Keep;
  descriptor.depthStencil.stencilFront.failOp = StencilOperation::Keep;
  descriptor.depthStencil.stencilFront.depthFailOp = StencilOperation::Keep;
  descriptor.depthStencil.stencilReadMask = 0xFFFFFFFF;
  descriptor.depthStencil.stencilWriteMask = 0xFFFFFFFF;
  descriptor.depthStencil.stencilBack = descriptor.depthStencil.stencilFront;
  descriptor.layout.uniformBlocks = {{"Args", 0, ShaderVisibility::Fragment}};
  return gpu->createRenderPipeline(descriptor);
}

bool StencilMaskRenderPass::draw(CommandEncoder* encoder, std::shared_ptr<Texture> renderTexture,
                                 std::shared_ptr<Texture> stencilTexture) const {
  auto gpu = encoder->gpu();
  if (gpu == nullptr || renderTexture == nullptr || stencilTexture == nullptr) {
    return false;
  }

  auto maskPipeline = createMaskPipeline(gpu);
  auto coverPipeline = createCoverPipeline(gpu);
  if (maskPipeline == nullptr || coverPipeline == nullptr) {
    return false;
  }

  // Both passes share one render-pass instance so the stencil buffer survives between them.
  // The colour attachment is cleared once at the top; the stencil attachment is cleared to 0
  // and inherits the writes from pass 1 into pass 2.
  RenderPassDescriptor descriptor(renderTexture, LoadAction::Clear, StoreAction::Store,
                                  PMColor::Transparent());
  descriptor.depthStencilAttachment =
      DepthStencilAttachment(stencilTexture, LoadAction::Clear, StoreAction::DontCare,
                             /*depthClearValue=*/1.0f,
                             /*depthReadOnly=*/false,
                             /*stencilClearValue=*/0,
                             /*stencilReadOnly=*/false);
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    return false;
  }

  // ---- Pass 1: write the stencil mask ----
  // GL/this RenderPass does not expose `TriangleFan` as a primitive type, so emit the disc
  // as an explicit triangle list. Each segment contributes one triangle (centre, perimeter_i,
  // perimeter_{i+1}); CIRCLE_SEGMENTS triangles in total.
  std::vector<VertexData> maskTriangles;
  maskTriangles.reserve(static_cast<size_t>(CIRCLE_SEGMENTS) * 3);
  const VertexData centre = {0.0f, 0.0f};
  for (int i = 0; i < CIRCLE_SEGMENTS; ++i) {
    auto angle0 = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) /
                  static_cast<float>(CIRCLE_SEGMENTS);
    auto angle1 = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i + 1) /
                  static_cast<float>(CIRCLE_SEGMENTS);
    maskTriangles.push_back(centre);
    maskTriangles.push_back(
        {CIRCLE_RADIUS_NDC * std::cos(angle0), CIRCLE_RADIUS_NDC * std::sin(angle0)});
    maskTriangles.push_back(
        {CIRCLE_RADIUS_NDC * std::cos(angle1), CIRCLE_RADIUS_NDC * std::sin(angle1)});
  }
  auto triangleBufferSize = maskTriangles.size() * sizeof(VertexData);
  auto triangleBuffer = gpu->createBuffer(triangleBufferSize, GPUBufferUsage::VERTEX);
  if (triangleBuffer == nullptr) {
    renderPass->end();
    return false;
  }
  auto triangleMapped = static_cast<VertexData*>(triangleBuffer->map());
  if (triangleMapped == nullptr) {
    renderPass->end();
    return false;
  }
  memcpy(triangleMapped, maskTriangles.data(), triangleBufferSize);
  triangleBuffer->unmap();

  renderPass->setPipeline(maskPipeline);
  renderPass->setStencilReference(1);
  renderPass->setVertexBuffer(0, triangleBuffer);
  renderPass->draw(PrimitiveType::Triangles, static_cast<uint32_t>(maskTriangles.size()));

  // ---- Pass 2: cover, with stencil test ----
  VertexData coverVertices[] = {
      {-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
  };
  auto coverBufferSize = sizeof(coverVertices);
  auto coverBuffer = gpu->createBuffer(coverBufferSize, GPUBufferUsage::VERTEX);
  if (coverBuffer == nullptr) {
    renderPass->end();
    return false;
  }
  auto coverMapped = static_cast<VertexData*>(coverBuffer->map());
  if (coverMapped == nullptr) {
    renderPass->end();
    return false;
  }
  memcpy(coverMapped, coverVertices, coverBufferSize);
  coverBuffer->unmap();

  // std140 layout: a single vec4 occupies exactly 16 bytes. Mirror the GLSL layout with a
  // float[4] so the field semantics are unambiguous (PMColor stores normalized floats, not
  // 0-255 bytes), and size the buffer from the array itself.
  const float coverColorData[4] = {coverColor.red, coverColor.green, coverColor.blue,
                                   coverColor.alpha};
  auto uniformBuffer = gpu->createBuffer(sizeof(coverColorData), GPUBufferUsage::UNIFORM);
  if (uniformBuffer == nullptr) {
    renderPass->end();
    return false;
  }
  auto uniformMapped = uniformBuffer->map();
  if (uniformMapped == nullptr) {
    renderPass->end();
    return false;
  }
  memcpy(uniformMapped, coverColorData, sizeof(coverColorData));
  uniformBuffer->unmap();

  renderPass->setPipeline(coverPipeline);
  renderPass->setStencilReference(1);
  renderPass->setVertexBuffer(0, coverBuffer);
  renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformBuffer->size());
  renderPass->draw(PrimitiveType::Triangles, 6);

  renderPass->end();
  return true;
}

}  // namespace tgfx
