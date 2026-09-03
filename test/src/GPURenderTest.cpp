/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <memory>
#include <string>
#include <vector>
#include "ArrayVaryingEffect.h"
#include "InstancedGridRenderPass.h"
#include "MultisampleTestEffect.h"
#include "StencilMaskRenderPass.h"
#include "gpu/ProgramInfo.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/resources/DepthStencilTextureView.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Texture.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

// Vertex shader: emits a full-screen quad and passes three constant scalar varyings to the
// fragment stage. The values are constants (not derived from vertex attributes) so rasterizer
// interpolation cannot perturb them — the fragment stage must see the exact same 1.0 / 0.5 /
// 0.25 triple across every pixel, isolating the varying-matching contract from any numerical
// noise.
static constexpr char VARYING_ORDER_VERTEX_SHADER[] = R"(
        in vec2 inPosition;

        out float vRed;
        out float vGreen;
        out float vBlue;

        void main() {
            gl_Position = vec4(inPosition, 0.0, 1.0);
            vRed = 1.0;
            vGreen = 0.5;
            vBlue = 0.25;
        }
    )";

// Fragment shader: deliberately declares the three `in` varyings in a DIFFERENT order from the
// vertex stage's `out` declarations (vertex: Red / Green / Blue; fragment: Blue / Red / Green).
// GL matches varyings by name, so the fragment reads back {1.0, 0.5, 0.25} and writes
// (255, 128, 64, 255). SPIR-V-based backends (Metal, Vulkan, D3D12) match by location — with
// ShaderCompiler's current per-stage `assignInputLocationQualifiers` /
// `assignOutputLocationQualifiers` policy each stage's locations are assigned in source order,
// so the fragment reads the wrong varying for each channel and produces a visibly different
// colour. This test pins the tgfx-level contract that a RuntimeEffect written in GLSL renders
// the same on every backend regardless of varying declaration order.
static constexpr char VARYING_ORDER_FRAGMENT_SHADER[] = R"(
        precision mediump float;

        in float vBlue;
        in float vRed;
        in float vGreen;

        out vec4 tgfx_FragColor;

        void main() {
            tgfx_FragColor = vec4(vRed, vGreen, vBlue, 1.0);
        }
    )";

struct VaryingOrderVertex {
  float x;
  float y;
};

static std::string PrefixShaderVersion(const char* body, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + body;
  }
  return std::string("#version 300 es\n\n") + body;
}

static std::shared_ptr<RenderPipeline> CreateVaryingOrderPipeline(GPU* gpu) {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(VARYING_ORDER_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(VARYING_ORDER_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;

  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  return gpu->createRenderPipeline(descriptor);
}

// Vertex shader: draws a full-screen quad using a scale factor pulled from a vertex-only UBO. The
// UBO's payload beyond scale.xy is padding whose bytes are chosen so that if a location-mismatch
// bug on a SPIR-V based backend causes the fragment stage to accidentally read this buffer, the
// resulting pixel colour is obviously different from the intended green output — see the
// UNIFORM_BUFFER_BINDING_FRAGMENT_SHADER commentary below.
static constexpr char UNIFORM_BUFFER_BINDING_VERTEX_SHADER[] = R"(
        in vec2 inPosition;

        layout(std140) uniform VertexArgs {
            vec4 scaleAndFiller;
        };

        void main() {
            gl_Position = vec4(inPosition * scaleAndFiller.xy, 0.0, 1.0);
        }
    )";

// Fragment shader: writes a solid colour taken from a fragment-only UBO. Combined with the CPU
// side declaring `uniformBlocks = {{"VertexArgs", 0, Vertex}, {"FragmentArgs", 1, Fragment}}` and
// calls to `setUniformBuffer(0, vertBuf, ...)` / `setUniformBuffer(1, fragBuf, ...)`, this pins
// the tgfx-level contract that each declared UBO binding number identifies a specific buffer
// regardless of which stage(s) actually read it.
//
// GL matches uniform blocks by name at link time via `glUniformBlockBinding`, so it renders the
// intended solid colour. Before the binding resolution was unified across backends, SPIR-V based
// backends (Metal, Vulkan / D3D12 by symmetry) matched UBO binding numbers per-stage: ShaderCompiler
// numbered each stage's custom UBOs from 0 in source order, so on Metal the fragment stage's
// `FragmentArgs` ended up at Metal buffer index 0 while the CPU wrote the fragment buffer at index
// 1. The fragment shader then read the wrong slot and picked up whatever happened to sit at index 0
// (nothing / garbage / the vertex buffer), producing a non-green pixel. This test pins the corrected
// name-keyed resolution.
static constexpr char UNIFORM_BUFFER_BINDING_FRAGMENT_SHADER[] = R"(
        precision mediump float;

        layout(std140) uniform FragmentArgs {
            vec4 color;
        };

        out vec4 tgfx_FragColor;

        void main() {
            tgfx_FragColor = color;
        }
    )";

struct UniformBufferBindingVertex {
  float x;
  float y;
};

struct UniformBufferBindingVertexArgs {
  float scaleX;
  float scaleY;
  float fillerZ;
  float fillerW;
};

struct UniformBufferBindingFragmentArgs {
  float r;
  float g;
  float b;
  float a;
};

static std::shared_ptr<RenderPipeline> CreateUniformBufferBindingPipeline(GPU* gpu,
                                                                          bool explicitLayout) {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;

  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  // Two distinct binding numbers, each visible to exactly one stage. This is the exact shape
  // libpag's MotionBlurFilter uses and the reason the current tgfx UBO-binding contract leaks
  // per-stage numbering semantics onto callers on SPIR-V based backends.
  if (explicitLayout) {
    descriptor.layout.uniformBlocks = {{"VertexArgs", 0, ShaderVisibility::Vertex},
                                       {"FragmentArgs", 1, ShaderVisibility::Fragment}};
  }

  return gpu->createRenderPipeline(descriptor);
}

// Vertex shader emitting an extra `vUnused` output that the fragment never consumes. Its name
// sorts lexicographically before `vUsed`, so per-stage name-sorted locations would shift `vUsed`
// on the vertex side only and desync the two stages.
static constexpr char MISMATCH_VERTEX_SHADER[] = R"(
        in vec2 inPosition;

        out float vUsed;
        out float vUnused;

        void main() {
            gl_Position = vec4(inPosition, 0.0, 1.0);
            vUsed = 1.0;
            vUnused = 0.0;
        }
    )";

static constexpr char MISMATCH_FRAGMENT_SHADER[] = R"(
        precision mediump float;

        in float vUsed;

        out vec4 tgfx_FragColor;

        void main() {
            tgfx_FragColor = vec4(vUsed, 0.0, 0.0, 1.0);
        }
    )";

static std::shared_ptr<RenderPipeline> CreateMismatchedVaryingPipeline(GPU* gpu) {
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(MISMATCH_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(MISMATCH_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;

  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  return gpu->createRenderPipeline(descriptor);
}

}  // namespace

// ==================== GPU Tests ====================

TGFX_TEST(GPURenderTest, DepthRenderPassTest) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  TextureDescriptor depthTextureDesc(110, 110, PixelFormat::DEPTH24_STENCIL8, false, 1,
                                     TextureUsage::RENDER_ATTACHMENT);
  auto depthTexture = context->gpu()->createTexture(depthTextureDesc);
  ASSERT_TRUE(depthTexture != nullptr);
  TextureDescriptor renderTextureDesc(
      110, 110, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = context->gpu()->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);
  RenderPassDescriptor renderPassDescriptor(renderTexture);
  renderPassDescriptor.depthStencilAttachment.texture = depthTexture;
  auto commandEncoder = context->gpu()->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);
  auto renderPass = commandEncoder->beginRenderPass(renderPassDescriptor);
  ASSERT_TRUE(renderPass != nullptr);
}

TGFX_TEST(GPURenderTest, InstancedGridRender) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();

  // Create render texture
  constexpr uint32_t rows = 100;
  constexpr uint32_t columns = 100;
  const int width =
      static_cast<int>(InstancedGridRenderPass::GRID_SIZE * static_cast<float>(columns) +
                       InstancedGridRenderPass::GRID_SPACING * static_cast<float>(columns - 1));
  const int height =
      static_cast<int>(InstancedGridRenderPass::GRID_SIZE * static_cast<float>(rows) +
                       InstancedGridRenderPass::GRID_SPACING * static_cast<float>(rows - 1));

  TextureDescriptor renderTextureDesc(
      width, height, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  // Create render pass
  auto commandEncoder = gpu->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);
  auto renderPass = InstancedGridRenderPass::Make(rows, columns);
  ASSERT_TRUE(renderPass->onDraw(commandEncoder.get(), renderTexture));

  // Submit and wait for completion
  auto commandBuffer = commandEncoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);

  // Also compare with baseline
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/InstancedGridRender"));
}

// ==================== Stencil Pipeline Tests ====================

// End-to-end exercise of the stencil-aware pipeline plumbing introduced for the bezier
// rasterization render path: ProgramInfo's stencil/colourWriteMask cache key contributions,
// GLSLProgramBuilder forwarding `depthStencil` to the pipeline descriptor, and the GL
// stencil-state factory's no-op detection (which previously dropped any pipeline whose
// compare function was Always — exactly the configuration the mask pass uses).
//
// Pass 1 stamps a centred disc into the stencil buffer with compare=Always +
// passOp=Replace; pass 2 fills the entire viewport with red but is gated on stencil==1.
// Reading back the rendered pixels lets us verify both that the mask was written (centre
// pixel is red) and that the stencil test is precise (corner pixel is the clear colour).
TGFX_TEST(GPURenderTest, StencilMaskRenderPass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  constexpr int SIZE = 200;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  TextureDescriptor stencilTextureDesc(SIZE, SIZE, PixelFormat::DEPTH24_STENCIL8, false, 1,
                                       TextureUsage::RENDER_ATTACHMENT);
  auto stencilTexture = gpu->createTexture(stencilTextureDesc);
  ASSERT_TRUE(stencilTexture != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  // Cover colour: opaque red, in premultiplied space.
  auto coverColor = Color{1.0f, 0.0f, 0.0f, 1.0f}.premultiply();
  auto pass = StencilMaskRenderPass::Make(coverColor);
  ASSERT_TRUE(pass != nullptr);
  ASSERT_TRUE(pass->draw(encoder.get(), renderTexture, stencilTexture));

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  // Sample two pixels: the surface centre (well inside the disc, must be red) and a corner
  // (well outside the disc, must be the clear colour transparent black). The disc has
  // radius 0.6 in NDC, so a pixel 4 pixels in from the corner sits at NDC ~(-0.96, -0.96)
  // — comfortably outside.
  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t centrePixel = 0;
  ASSERT_TRUE(surface->readPixels(info, &centrePixel, SIZE / 2, SIZE / 2));
  uint32_t cornerPixel = 0xDEADBEEF;
  ASSERT_TRUE(surface->readPixels(info, &cornerPixel, 4, 4));

  // RGBA_8888 byte order is R, G, B, A. The centre must read as opaque red; the corner must
  // read as the transparent-black clear colour.
  EXPECT_EQ(centrePixel, 0xFF0000FFu) << "centre pixel was not the opaque red cover colour";
  EXPECT_EQ(cornerPixel, 0u) << "corner pixel was not the clear colour";
}

TGFX_TEST(GPURenderTest, RenderTargetProxyGetStencil) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  std::shared_ptr<DepthStencilTextureView> shared = nullptr;
  {
    // Case 1: the attachment is lazily created on the first getStencil() call and reused
    // within the proxy on subsequent calls.
    auto proxy = RenderTargetProxy::Make(context, 96, 64, /*alphaOnly=*/false);
    ASSERT_TRUE(proxy != nullptr);
    shared = proxy->getStencil(1);
    ASSERT_TRUE(shared != nullptr);
    EXPECT_EQ(shared->width(), 96);
    EXPECT_EQ(shared->height(), 64);
    EXPECT_EQ(shared->sampleCount(), 1);
    auto second = proxy->getStencil(1);
    ASSERT_TRUE(second != nullptr);
    // Lazy-init contract: subsequent calls return the cached instance, not a fresh attachment.
    EXPECT_EQ(shared.get(), second.get());
  }

  {
    // Case 2: a proxy with the same spec reuses the cached attachment while it is still in
    // the nonpurgeable list.
    auto proxy = RenderTargetProxy::Make(context, 96, 64, /*alphaOnly=*/false);
    ASSERT_TRUE(proxy != nullptr);
    auto stencil = proxy->getStencil(1);
    ASSERT_TRUE(stencil != nullptr);
    EXPECT_EQ(stencil.get(), shared.get());
  }

  // Release the last strong reference so the attachment enters the purgeable queue.
  auto* released = shared.get();
  shared = nullptr;
  {
    // Case 3: a proxy with the same spec reactivates the same attachment from the purgeable
    // queue.
    auto proxy = RenderTargetProxy::Make(context, 96, 64, /*alphaOnly=*/false);
    ASSERT_TRUE(proxy != nullptr);
    auto stencil = proxy->getStencil(1);
    ASSERT_TRUE(stencil != nullptr);
    EXPECT_EQ(stencil.get(), released);
    shared = stencil;
  }

  {
    // Case 4: a proxy with a different spec gets a separate attachment.
    auto proxy = RenderTargetProxy::Make(context, 128, 64, /*alphaOnly=*/false);
    ASSERT_TRUE(proxy != nullptr);
    auto stencil = proxy->getStencil(1);
    ASSERT_TRUE(stencil != nullptr);
    EXPECT_NE(stencil.get(), shared.get());
  }
}

// Verifies the setter/getter pair for ProgramInfo's stencil and colour-write-mask state, and
// that the colour write mask is propagated into the PipelineColorAttachment that
// GLSLProgramBuilder forwards to the backend. Without this, draw ops that need stencil-only
// passes (e.g. the bezier-rasterization stencil pass) could call setColorWriteMask(0) yet
// still emit colour writes downstream.
TGFX_TEST(GPURenderTest, ProgramInfoStencilAccessors) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto proxy = RenderTargetProxy::Make(context, 1, 1, /*alphaOnly=*/false);
  ASSERT_TRUE(proxy != nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_TRUE(renderTarget != nullptr);
  auto* allocator = context->drawingAllocator();
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  ASSERT_TRUE(geometryProcessor != nullptr);
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  ProgramInfo info(renderTarget.get(), geometryProcessor.get(), std::move(fragmentProcessors),
                   /*numColorProcessors=*/0, /*xferProcessor=*/nullptr, BlendMode::SrcOver);

  // Defaults: no-op stencil, all colour channels writable.
  EXPECT_EQ(info.getColorWriteMask(), ColorWriteMask::All);
  EXPECT_EQ(info.getDepthStencil().format, PixelFormat::Unknown);
  EXPECT_EQ(info.getPipelineColorAttachment().colorWriteMask, ColorWriteMask::All);

  info.setColorWriteMask(0);
  DepthStencilDescriptor stencilDS = {};
  stencilDS.format = PixelFormat::DEPTH24_STENCIL8;
  stencilDS.stencilFront.compare = CompareFunction::Equal;
  stencilDS.stencilFront.passOp = StencilOperation::Replace;
  stencilDS.stencilBack = stencilDS.stencilFront;
  info.setDepthStencil(stencilDS);

  EXPECT_EQ(info.getColorWriteMask(), 0u);
  EXPECT_EQ(info.getDepthStencil().format, PixelFormat::DEPTH24_STENCIL8);
  EXPECT_EQ(info.getDepthStencil().stencilFront.compare, CompareFunction::Equal);
  EXPECT_EQ(info.getDepthStencil().stencilFront.passOp, StencilOperation::Replace);
  // The colour write mask must reach the PipelineColorAttachment that the program builder
  // forwards to the backend.
  EXPECT_EQ(info.getPipelineColorAttachment().colorWriteMask, 0u);
}

// Verifies that ProgramInfo::getProgram()'s cache key distinguishes pipelines that share
// shaders but differ in colour write mask or stencil configuration. Otherwise the bezier
// rasterization stencil/cover passes would silently collapse onto a single cached program.
// This pins down the program-cache key encoding added for the stencil plumbing so any future
// regression there surfaces here, not at the much harder-to-diagnose draw call site.
TGFX_TEST(GPURenderTest, ProgramInfoStencilCacheKey) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto proxy = RenderTargetProxy::Make(context, 1, 1, /*alphaOnly=*/false);
  ASSERT_TRUE(proxy != nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_TRUE(renderTarget != nullptr);
  auto* allocator = context->drawingAllocator();

  // Each variant builds its own GeometryProcessor with the same parameters; the GP's key
  // contribution is content-addressed, so the three GPs hash identically and only the stencil
  // and colour-write-mask overlay should differentiate the cache lookups below.
  auto gpA = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpA2 = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpB = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpC = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  ASSERT_TRUE(gpA && gpA2 && gpB && gpC);

  DepthStencilDescriptor stencilDS = {};
  stencilDS.format = PixelFormat::DEPTH24_STENCIL8;
  stencilDS.stencilFront.compare = CompareFunction::Equal;
  stencilDS.stencilFront.passOp = StencilOperation::Replace;
  stencilDS.stencilBack = stencilDS.stencilFront;

  // Variant A: defaults.
  ProgramInfo infoA(renderTarget.get(), gpA.get(), {}, 0, nullptr, BlendMode::SrcOver);
  auto programA = infoA.getProgram();
  ASSERT_TRUE(programA != nullptr);

  // Variant A again: equivalent inputs must hit the same cached program.
  ProgramInfo infoA2(renderTarget.get(), gpA2.get(), {}, 0, nullptr, BlendMode::SrcOver);
  auto programA2 = infoA2.getProgram();
  EXPECT_EQ(programA2.get(), programA.get())
      << "identical ProgramInfo inputs should reuse the cached Program";

  // Variant B: differ only in colour write mask.
  ProgramInfo infoB(renderTarget.get(), gpB.get(), {}, 0, nullptr, BlendMode::SrcOver);
  infoB.setColorWriteMask(0);
  auto programB = infoB.getProgram();
  ASSERT_TRUE(programB != nullptr);
  EXPECT_NE(programB.get(), programA.get())
      << "differing colorWriteMask must not collapse onto the same cached Program";

  // Variant C: differ only in stencil configuration.
  ProgramInfo infoC(renderTarget.get(), gpC.get(), {}, 0, nullptr, BlendMode::SrcOver);
  infoC.setDepthStencil(stencilDS);
  auto programC = infoC.getProgram();
  ASSERT_TRUE(programC != nullptr);
  EXPECT_NE(programC.get(), programA.get())
      << "differing depthStencil must not collapse onto the same cached Program";
  EXPECT_NE(programC.get(), programB.get())
      << "differing depthStencil must not collapse with a colorWriteMask-only variant";
}

// ==================== Multisample Tests ====================

TGFX_TEST(GPURenderTest, MultisampleCount) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // sampleCount=1: no MSAA, the diagonal edge should have hard aliased pixels.
  MultisampleConfig config1x = {};
  config1x.sampleCount = 1;
  config1x.outputColor = Color::Red();
  auto effect1x = MultisampleTestEffect::Make(config1x);
  auto filter1x = ImageFilter::Runtime(std::move(effect1x));
  auto image1x = image->makeWithFilter(std::move(filter1x));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(image1x));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleCount_1x"));

  // sampleCount=4: MSAA enabled, the diagonal edge should have smooth anti-aliased pixels.
  MultisampleConfig config4x = {};
  config4x.sampleCount = 4;
  config4x.outputColor = Color::Red();
  auto effect4x = MultisampleTestEffect::Make(config4x);
  auto filter4x = ImageFilter::Runtime(std::move(effect4x));
  auto image4x = image->makeWithFilter(std::move(filter4x));
  canvas->clear();
  canvas->drawImage(std::move(image4x));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleCount_4x"));
}

TGFX_TEST(GPURenderTest, MultisampleMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // mask=0xFFFFFFFF: all samples enabled, should render the red triangle normally.
  MultisampleConfig configAllSamples = {};
  configAllSamples.sampleCount = 4;
  configAllSamples.sampleMask = 0xFFFFFFFF;
  configAllSamples.outputColor = Color::Red();
  auto effectAll = MultisampleTestEffect::Make(configAllSamples);
  auto filterAll = ImageFilter::Runtime(std::move(effectAll));
  auto imageAll = image->makeWithFilter(std::move(filterAll));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(imageAll));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleMask_AllSamples"));

  // mask=0x0: no samples written, the result should be the clear color (transparent).
  MultisampleConfig configNoSamples = {};
  configNoSamples.sampleCount = 4;
  configNoSamples.sampleMask = 0x0;
  configNoSamples.outputColor = Color::Red();
  auto effectNone = MultisampleTestEffect::Make(configNoSamples);
  auto filterNone = ImageFilter::Runtime(std::move(effectNone));
  auto imageNone = image->makeWithFilter(std::move(filterNone));
  canvas->clear();
  canvas->drawImage(std::move(imageNone));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleMask_NoSamples"));
}

TGFX_TEST(GPURenderTest, AlphaToCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // alphaToCoverage=false with alpha=0.5: all 4 samples get (0.5,0,0,0.5), resolve = (0.5,0,0,0.5)
  MultisampleConfig configOff = {};
  configOff.sampleCount = 4;
  configOff.outputColor = {1.0f, 0.0f, 0.0f, 0.5f};
  configOff.alphaToCoverage = false;
  auto effectOff = MultisampleTestEffect::Make(configOff);
  auto filterOff = ImageFilter::Runtime(std::move(effectOff));
  auto imageOff = image->makeWithFilter(std::move(filterOff));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(imageOff));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/AlphaToCoverage_Off"));

  // alphaToCoverage=true with alpha=0.5: alpha drives coverage, ~2 of 4 samples written,
  // resolve produces a different (typically dimmer) result than alphaToCoverage=false.
  MultisampleConfig configOn = {};
  configOn.sampleCount = 4;
  configOn.outputColor = {1.0f, 0.0f, 0.0f, 0.5f};
  configOn.alphaToCoverage = true;
  auto effectOn = MultisampleTestEffect::Make(configOn);
  auto filterOn = ImageFilter::Runtime(std::move(effectOn));
  auto imageOn = image->makeWithFilter(std::move(filterOn));
  canvas->clear();
  canvas->drawImage(std::move(imageOn));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/AlphaToCoverage_On"));
}

TGFX_TEST(GPURenderTest, ArrayVarying) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Use a 200x200 synthetic input so the filtered image is also 200x200. The effect draws an NDC
  // [-0.5, 0.5] quad, which lands a 100x100 rectangle centred in the 200x200 surface with a
  // 50-pixel margin on every side. The input pixel values are irrelevant — the effect ignores
  // its source texture and overwrites the entire filtered image with the blue quad.
  Bitmap inputBitmap(200, 200, false);
  auto image = Image::MakeFrom(std::move(inputBitmap));
  ASSERT_TRUE(image != nullptr);

  auto effect = ArrayVaryingEffect::Make();
  auto filter = ImageFilter::Runtime(std::move(effect));
  auto filtered = image->makeWithFilter(std::move(filter));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(filtered));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/ArrayVarying"));
}

// Reproduces a cross-backend rendering divergence caused by declaring vertex `out` and
// fragment `in` varyings in different orders. GL links varyings by name and renders the
// intended colour; SPIR-V-based backends (Metal via GLSL → SPIR-V → MSL, Vulkan, D3D12) link
// by location, and tgfx's ShaderCompiler currently assigns locations per-stage in source
// order — so the mismatched order silently swaps channels on those backends.
//
// The vertex shader emits three constant scalar varyings (vRed=1.0, vGreen=0.5, vBlue=0.25)
// and the fragment shader declares them in a shuffled order (vBlue / vRed / vGreen) before
// writing vec4(vRed, vGreen, vBlue, 1.0). The centred quad means every rendered pixel carries the
// same colour, so the baseline captures a flat swatch surrounded by transparency and any
// location-order bug shows up as the swapped colour (Metal today renders vec4(vBlue, vRed,
// vGreen, 1.0)).
TGFX_TEST(GPURenderTest, VaryingOrderMismatch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  constexpr int SIZE = 200;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);

  auto pipeline = CreateVaryingOrderPipeline(gpu);
  ASSERT_TRUE(pipeline != nullptr);

  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);
  renderPass->setPipeline(std::move(pipeline));

  // A centred quad covering the middle half of NDC in both axes. On the 200x200 surface this
  // yields a 100x100 rectangle with a 50-pixel margin on every side.
  VaryingOrderVertex quadVertices[] = {
      {-0.5f, -0.5f},
      {0.5f, -0.5f},
      {-0.5f, 0.5f},
      {0.5f, 0.5f},
  };
  auto vertexBuffer = gpu->createBuffer(sizeof(quadVertices), GPUBufferUsage::VERTEX);
  ASSERT_TRUE(vertexBuffer != nullptr);
  auto* mapped = static_cast<VaryingOrderVertex*>(vertexBuffer->map());
  ASSERT_TRUE(mapped != nullptr);
  memcpy(mapped, quadVertices, sizeof(quadVertices));
  vertexBuffer->unmap();

  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->draw(PrimitiveType::TriangleStrip, 4, 1);
  renderPass->end();

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/VaryingOrderMismatch"));
}

// The vertex stage emits an extra output that the fragment never consumes. SPIR-V based backends
// (Metal, Vulkan, D3D12, WebGPU) must reject the pipeline at creation instead of silently reading
// undefined values; OpenGL links varyings by name and silently drops the unused output.
TGFX_TEST(GPURenderTest, VaryingMismatchRejected) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();

  auto backend = gpu->info()->backend;

  auto pipeline = CreateMismatchedVaryingPipeline(gpu);

  if (backend == Backend::OpenGL) {
    EXPECT_TRUE(pipeline != nullptr);
  } else {
    EXPECT_TRUE(pipeline == nullptr);
  }
}

// Reproduces a cross-backend rendering divergence caused by declaring one custom UBO per stage
// with distinct CPU-side binding numbers. libpag hits this today in MotionBlurFilter, where the
// vertex shader has one UBO (matrix) and the fragment shader has a different one (blur
// parameters), declared as `{{"VertexArgs", 0, Vertex}, {"FragmentArgs", 1, Fragment}}`.
//
// GL binds uniform blocks by name via `glUniformBlockBinding` at link time, so the fragment
// stage sees `FragmentArgs` at binding 1 exactly as the CPU wrote it. Before binding resolution
// was unified, SPIR-V based backends matched by numeric binding: ShaderCompiler numbered custom
// UBOs per-stage starting at 0 in source order, so the fragment stage's `FragmentArgs` ended up at
// binding 0. Metal then bound the fragment buffer at its own per-stage index 1 (what the CPU asked
// for), but the shader read index 0 and picked up garbage / the vertex buffer. The baseline
// captures the intended solid green output; a per-stage numbering leak shows up as a wildly
// different colour.
TGFX_TEST(GPURenderTest, UniformBufferBindingMismatch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  constexpr int SIZE = 200;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);

  auto pipeline = CreateUniformBufferBindingPipeline(gpu, true);
  ASSERT_TRUE(pipeline != nullptr);

  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);
  renderPass->setPipeline(std::move(pipeline));

  // Full-screen source quad in NDC as a TriangleStrip: BL, BR, TL, TR. The vertex UBO scales it
  // to a centered 100x100 rectangle with a 50-pixel margin on each side.
  UniformBufferBindingVertex quadVertices[] = {
      {-1.0f, -1.0f},
      {1.0f, -1.0f},
      {-1.0f, 1.0f},
      {1.0f, 1.0f},
  };
  auto vertexBuffer = gpu->createBuffer(sizeof(quadVertices), GPUBufferUsage::VERTEX);
  ASSERT_TRUE(vertexBuffer != nullptr);
  auto* mappedVertices = static_cast<UniformBufferBindingVertex*>(vertexBuffer->map());
  ASSERT_TRUE(mappedVertices != nullptr);
  memcpy(mappedVertices, quadVertices, sizeof(quadVertices));
  vertexBuffer->unmap();

  // Vertex UBO: scaleAndFiller = (0.5, 0.5, 1.0, 1.0). If a binding-number mismatch causes the
  // fragment stage to accidentally read this buffer as its `color`, the output is visibly
  // different from the intended green.
  auto vertexUniformBuffer =
      gpu->createBuffer(sizeof(UniformBufferBindingVertexArgs), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(vertexUniformBuffer != nullptr);
  auto* vertexArgs = static_cast<UniformBufferBindingVertexArgs*>(vertexUniformBuffer->map());
  ASSERT_TRUE(vertexArgs != nullptr);
  vertexArgs->scaleX = 0.5f;
  vertexArgs->scaleY = 0.5f;
  vertexArgs->fillerZ = 1.0f;
  vertexArgs->fillerW = 1.0f;
  vertexUniformBuffer->unmap();

  // Fragment UBO: solid green colour.
  auto fragmentUniformBuffer =
      gpu->createBuffer(sizeof(UniformBufferBindingFragmentArgs), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(fragmentUniformBuffer != nullptr);
  auto* fragmentArgs = static_cast<UniformBufferBindingFragmentArgs*>(fragmentUniformBuffer->map());
  ASSERT_TRUE(fragmentArgs != nullptr);
  fragmentArgs->r = 0.0f;
  fragmentArgs->g = 1.0f;
  fragmentArgs->b = 0.0f;
  fragmentArgs->a = 1.0f;
  fragmentUniformBuffer->unmap();

  renderPass->setVertexBuffer(0, vertexBuffer);
  // The CPU-declared bindings (0 for vertex, 1 for fragment) are treated as global identifiers by
  // the tgfx API — the same contract GL enforces via glUniformBlockBinding. Backends that treat
  // binding numbers as per-stage indices need internal translation; that translation is what this
  // test pins down.
  renderPass->setUniformBuffer(0, vertexUniformBuffer, 0, vertexUniformBuffer->size());
  renderPass->setUniformBuffer(1, fragmentUniformBuffer, 0, fragmentUniformBuffer->size());
  renderPass->draw(PrimitiveType::TriangleStrip, 4, 1);
  renderPass->end();

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/UniformBufferBindingMismatch"));
}

// Verifies that createRenderPipeline rejects a layout that omits a uniform block a shader stage
// declares. Symmetric to VaryingMismatchRejected: silently accepting the pipeline would leave the
// fragment `FragmentArgs` unbound at draw time on SPIR-V backends. OpenGL binds uniform blocks by
// name at link time and simply reads undefined data from an unbound block, so it is expected to
// still succeed pipeline creation there — the check is enforced by ResolveUniformSlots and only
// runs on the SPIR-V based backends (Metal / Vulkan / D3D12 / WebGPU).
TGFX_TEST(GPURenderTest, UniformBufferMissingDeclarationRejected) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);
  auto backend = gpu->info()->backend;
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  ASSERT_TRUE(vertexShader != nullptr);

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  ASSERT_TRUE(fragmentShader != nullptr);

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  // Layout intentionally omits the fragment shader's `FragmentArgs` block.
  descriptor.layout.uniformBlocks = {{"VertexArgs", 0, ShaderVisibility::Vertex}};

  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (backend == Backend::OpenGL) {
    EXPECT_TRUE(pipeline != nullptr);
  } else {
    EXPECT_TRUE(pipeline == nullptr);
  }
}

// Two distinct blocks — vertex-only `VertexArgs` and fragment-only `FragmentArgs` — declared with
// the same logical binding number in the pipeline layout. Every SPIR-V backend keys its
// per-draw `setUniformBuffer(binding, ...)` off that logical number and stores at most one
// UniformSlotMapping per number, so silently accepting a duplicate would guarantee that whichever
// entry lost the tie could never receive data at draw time. The pipeline-time uniqueness check
// exists precisely to eliminate that silent misbind; OpenGL takes a different code path (a linked
// program's block-to-binding-point map has no room for the collision).
TGFX_TEST(GPURenderTest, UniformBufferDuplicateLogicalBinding) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);
  auto backend = gpu->info()->backend;
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  ASSERT_TRUE(vertexShader != nullptr);

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(UNIFORM_BUFFER_BINDING_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  ASSERT_TRUE(fragmentShader != nullptr);

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  // Two different blocks share logical binding 0 — the SPIR-V backends must reject this.
  descriptor.layout.uniformBlocks = {{"VertexArgs", 0, ShaderVisibility::Vertex},
                                     {"FragmentArgs", 0, ShaderVisibility::Fragment}};

  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (backend == Backend::OpenGL) {
    EXPECT_TRUE(pipeline != nullptr);
  } else {
    EXPECT_TRUE(pipeline == nullptr);
  }
}

// Vertex shader declaring two uniform blocks (`PreArgs` then `SharedArgs`) so `SharedArgs`
// occupies physical slot 1 in the vertex stage, while the fragment shader declares only
// `SharedArgs` and puts it at physical slot 0. `SharedArgs` is a same-name cross-stage block
// (visibility = VertexFragment), which is the only path that produces a UniformSlotMapping with
// non-matching vertexSlot / fragmentSlot values — and, on D3D12, two CBV root parameters that
// bind different registers for the same logical binding. Both blocks are read in the vertex body
// so no driver can dead-strip PreArgs and collapse SharedArgs back to slot 0.
static constexpr char SHARED_STAGGERED_VERTEX_SHADER[] = R"(
        precision mediump float;

        in vec2 inPosition;

        layout(std140) uniform PreArgs {
            vec4 vertScale;
        };

        layout(std140) uniform SharedArgs {
            vec4 scaleAndFiller;
            vec4 color;
        };

        void main() {
            // Final scale = PreArgs.vertScale.xy * SharedArgs.scaleAndFiller.xy. CPU feeds
            // (0.5, 0.5) * (1.0, 1.0) = 0.5, producing a centered 100x100 quad. If the
            // pipeline mis-binds either block, the observable scale changes visibly.
            gl_Position =
                vec4(inPosition * vertScale.xy * scaleAndFiller.xy, 0.0, 1.0);
        }
    )";

static constexpr char SHARED_STAGGERED_FRAGMENT_SHADER[] = R"(
        precision mediump float;

        layout(std140) uniform SharedArgs {
            vec4 scaleAndFiller;
            vec4 color;
        };

        out vec4 tgfx_FragColor;

        void main() {
            tgfx_FragColor = color;
        }
    )";

struct SharedStaggeredArgs {
  float scaleX;
  float scaleY;
  float fillerZ;
  float fillerW;
  float r;
  float g;
  float b;
  float a;
};

struct PreArgsData {
  float vertScaleX;
  float vertScaleY;
  float pad0;
  float pad1;
};

TGFX_TEST(GPURenderTest, UniformBufferSharedLayoutStaggeredSlots) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);
  auto info = gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  constexpr int SIZE = 200;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = PrefixShaderVersion(SHARED_STAGGERED_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  ASSERT_TRUE(vertexShader != nullptr);

  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = PrefixShaderVersion(SHARED_STAGGERED_FRAGMENT_SHADER, isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  ASSERT_TRUE(fragmentShader != nullptr);

  RenderPipelineDescriptor descriptor = {};
  Attribute position = {"inPosition", VertexFormat::Float2};
  VertexBufferLayout vertexLayout({position}, VertexStepMode::Vertex);
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  PipelineColorAttachment colorAttachment = {};
  colorAttachment.blendEnable = false;
  descriptor.fragment.colorAttachments.push_back(colorAttachment);

  // Logical binding 10 targets vertex-only PreArgs, logical binding 0 targets the shared
  // SharedArgs (visible to both stages). Distinct logical ids and the VertexFragment visibility
  // together drive the vertexSlot != fragmentSlot path in UniformSlotMapping.
  descriptor.layout.uniformBlocks = {{"PreArgs", 10, ShaderVisibility::Vertex},
                                     {"SharedArgs", 0, ShaderVisibility::VertexFragment}};

  auto pipeline = gpu->createRenderPipeline(descriptor);
  ASSERT_TRUE(pipeline != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);
  renderPass->setPipeline(std::move(pipeline));

  UniformBufferBindingVertex quadVertices[] = {
      {-1.0f, -1.0f},
      {1.0f, -1.0f},
      {-1.0f, 1.0f},
      {1.0f, 1.0f},
  };
  auto vertexBuffer = gpu->createBuffer(sizeof(quadVertices), GPUBufferUsage::VERTEX);
  ASSERT_TRUE(vertexBuffer != nullptr);
  auto* mappedVertices = static_cast<UniformBufferBindingVertex*>(vertexBuffer->map());
  ASSERT_TRUE(mappedVertices != nullptr);
  memcpy(mappedVertices, quadVertices, sizeof(quadVertices));
  vertexBuffer->unmap();

  // PreArgs.vertScale.xy = (0.5, 0.5): vertex reads this as one factor of the quad scale.
  auto preBuffer = gpu->createBuffer(sizeof(PreArgsData), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(preBuffer != nullptr);
  auto* preArgs = static_cast<PreArgsData*>(preBuffer->map());
  ASSERT_TRUE(preArgs != nullptr);
  preArgs->vertScaleX = 0.5f;
  preArgs->vertScaleY = 0.5f;
  preArgs->pad0 = 0.0f;
  preArgs->pad1 = 0.0f;
  preBuffer->unmap();

  // SharedArgs feeds both stages via a single setUniformBuffer(0) call: vertex multiplies its
  // scaleAndFiller = (1.0, 1.0) into the final scale so the quad ends up centered 100x100;
  // fragment paints it blue.
  auto sharedBuffer = gpu->createBuffer(sizeof(SharedStaggeredArgs), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(sharedBuffer != nullptr);
  auto* sharedArgs = static_cast<SharedStaggeredArgs*>(sharedBuffer->map());
  ASSERT_TRUE(sharedArgs != nullptr);
  sharedArgs->scaleX = 1.0f;
  sharedArgs->scaleY = 1.0f;
  sharedArgs->fillerZ = 0.0f;
  sharedArgs->fillerW = 0.0f;
  sharedArgs->r = 0.0f;
  sharedArgs->g = 0.0f;
  sharedArgs->b = 1.0f;
  sharedArgs->a = 1.0f;
  sharedBuffer->unmap();

  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->setUniformBuffer(10, preBuffer, 0, preBuffer->size());
  renderPass->setUniformBuffer(0, sharedBuffer, 0, sharedBuffer->size());
  renderPass->draw(PrimitiveType::TriangleStrip, 4, 1);
  renderPass->end();

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/UniformBufferSharedLayoutStaggeredSlots"));
}

}  // namespace tgfx
