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
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <emscripten.h>
#include <limits>
#include <string>
#include "gpu/webgpu/WebGPUGPU.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/ImageOrigin.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/RuntimeEffect.h"
#include "tgfx/gpu/Sampler.h"
#include "utils/TestUtils.h"

namespace tgfx {

static std::string PrefixShaderVersion(const char* body, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 150\n\n") + body;
  }
  return std::string("#version 300 es\n\n") + body;
}

// A full-screen quad in NDC (TriangleStrip: BL, BR, TL, TR). The fragment stage receives the
// quad position remapped to 0..1 texture coordinates.
static constexpr char QUAD_VERTEX_SHADER[] = R"(
    in vec2 aPosition;
    out vec2 vertexColor;
    void main() {
        gl_Position = vec4(aPosition, 0.0, 1.0);
        vertexColor = aPosition * 0.5 + 0.5;
    }
)";

static constexpr float QUAD_VERTICES[] = {
    -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
};

static std::shared_ptr<GPUBuffer> MakeQuadVertexBuffer(GPU* gpu) {
  auto vertexBuffer = gpu->createBuffer(sizeof(QUAD_VERTICES), GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    return nullptr;
  }
  auto* vertices = static_cast<float*>(vertexBuffer->map());
  if (vertices == nullptr) {
    return nullptr;
  }
  memcpy(vertices, QUAD_VERTICES, sizeof(QUAD_VERTICES));
  vertexBuffer->unmap();
  return vertexBuffer;
}

static std::shared_ptr<Image> MakeSolidImage(int size, uint32_t pmColor) {
  Bitmap bitmap(size, size, false, false);
  if (bitmap.isEmpty()) {
    return nullptr;
  }
  Pixmap pixmap(bitmap);
  auto* pixels = static_cast<uint32_t*>(pixmap.writablePixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  for (int i = 0; i < size * size; i++) {
    pixels[i] = pmColor;
  }
  return Image::MakeFrom(std::move(bitmap));
}

static void ExpectColor(const RGBA4f<AlphaType::Unpremultiplied>& color, float red, float green,
                        float blue, float alpha) {
  EXPECT_NEAR(color.red, red, 0.01f);
  EXPECT_NEAR(color.green, green, 0.01f);
  EXPECT_NEAR(color.blue, blue, 0.01f);
  EXPECT_NEAR(color.alpha, alpha, 0.01f);
}

// The GPUBuffer::size() contract must not be altered by the backend: a UNIFORM buffer created
// with a 12-byte capacity reports exactly 12 bytes. Rounding uniform capacities up to 16-byte
// multiples inside WebGPUBuffer::Make would silently change the public semantics and mask
// caller-side struct layout bugs (the 16-byte padding belongs to the std140 block layout, which
// callers must match).
TGFX_TEST(WebGPUUniformBindingTest, UniformBufferPreservesRequestedSize) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  auto buffer = gpu->createBuffer(12, GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_EQ(buffer->size(), static_cast<size_t>(12));
}

// Shared fixture state for the uniform binding draw tests.
struct UniformBindingTestEnv {
  Context* context = nullptr;
  GPU* gpu = nullptr;
  std::shared_ptr<Texture> renderTexture = nullptr;
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  std::shared_ptr<GPUBuffer> vertexBuffer = nullptr;
};

// Fragment stage variants:
// - Vec4Color: `Args { vec4 color; }` — a single vec4 naturally fills 16 bytes.
// - Vec2FloatTail: `Args { vec2 center; float amount; }` — std140 rounds the 12-byte member
//   layout up to 16 bytes (vec2 alignment), which is the shape libpag's RadialBlurFilter uses:
//   the caller must pad its struct to 16 bytes for the binding to pass validation.
enum class UniformBlockKind { Vec4Color, Vec2FloatTail };

static bool SetupUniformBindingTestEnv(UniformBindingTestEnv* env, Context* context, int size,
                                       UniformBlockKind kind) {
  env->context = context;
  env->gpu = context->gpu();
  auto info = env->gpu->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;

  TextureDescriptor textureDesc(size, size, PixelFormat::RGBA_8888, false, 1,
                                TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  env->renderTexture = env->gpu->createTexture(textureDesc);
  if (env->renderTexture == nullptr) {
    return false;
  }

  auto vertexModule = ShaderModuleDescriptor();
  vertexModule.code = PrefixShaderVersion(QUAD_VERTEX_SHADER, isDesktop);
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = env->gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return false;
  }

  static constexpr char VEC4_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec2 vertexColor;
        layout(std140) uniform Args {
            vec4 color;
        };
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = color;
        }
    )";
  static constexpr char VEC2_FLOAT_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec2 vertexColor;
        layout(std140) uniform Args {
            vec2 center;
            float amount;
        };
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = vec4(center, amount, 1.0);
        }
    )";
  auto fragmentModule = ShaderModuleDescriptor();
  fragmentModule.code = PrefixShaderVersion(
      kind == UniformBlockKind::Vec4Color ? VEC4_FRAGMENT_SHADER : VEC2_FLOAT_FRAGMENT_SHADER,
      isDesktop);
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = env->gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return false;
  }

  RenderPipelineDescriptor descriptor = {};
  descriptor.vertex.bufferLayouts = {VertexBufferLayout({{"aPosition", VertexFormat::Float2}},
                                                        VertexStepMode::Vertex)};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.layout.uniformBlocks = {{"Args", 0, ShaderVisibility::Fragment}};
  env->pipeline = env->gpu->createRenderPipeline(descriptor);
  if (env->pipeline == nullptr) {
    return false;
  }

  env->vertexBuffer = MakeQuadVertexBuffer(env->gpu);
  if (env->vertexBuffer == nullptr) {
    return false;
  }
  return true;
}

static bool DrawQuadWithUniformBinding(const UniformBindingTestEnv& env,
                                       std::shared_ptr<GPUBuffer> uniformBuffer, size_t offset,
                                       size_t size) {
  auto encoder = env.gpu->createCommandEncoder();
  if (encoder == nullptr) {
    EXPECT_TRUE(encoder != nullptr);
    return false;
  }
  RenderPassDescriptor renderPassDesc(env.renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  if (renderPass == nullptr) {
    EXPECT_TRUE(renderPass != nullptr);
    return false;
  }
  renderPass->setPipeline(env.pipeline);
  renderPass->setVertexBuffer(0, env.vertexBuffer);
  renderPass->setUniformBuffer(0, std::move(uniformBuffer), offset, size);
  renderPass->draw(PrimitiveType::TriangleStrip, 4);
  renderPass->end();
  auto commandBuffer = encoder->finish();
  if (commandBuffer == nullptr) {
    EXPECT_TRUE(commandBuffer != nullptr);
    return false;
  }
  env.gpu->queue()->submit(commandBuffer);
  env.gpu->queue()->waitUntilCompleted();
  return true;
}

// Reads the center pixel of the render target. Readback failures must fail the test instead of
// returning an expected color, otherwise "the draw was correctly skipped" and "the readback
// itself failed" become indistinguishable for cases whose expected color is transparent.
static bool ReadCenterPixel(const UniformBindingTestEnv& env, int size,
                            RGBA4f<AlphaType::Unpremultiplied>* color) {
  auto surface =
      Surface::MakeFrom(env.context, env.renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  if (surface == nullptr) {
    EXPECT_TRUE(surface != nullptr);
    return false;
  }
  Bitmap bitmap(size, size, false, false);
  Pixmap pixmap(bitmap);
  if (!surface->readPixels(pixmap.info(), pixmap.writablePixels())) {
    EXPECT_TRUE(false) << "readPixels failed";
    return false;
  }
  *color = pixmap.getColor(size / 2, size / 2);
  return true;
}

// Pops the pending validation error scope and awaits the browser's answer (Asyncify suspends
// the WASM stack while the Promise resolves). Returns 0 when the scope captured no error,
// 1 when it captured an error, and -1 when the device handle is invalid.
EM_ASYNC_JS(int, WebGPUPopErrorScope, (WGPUDevice deviceHandle), {
  var deviceWrapper = WebGPU.mgrDevice.objects[deviceHandle];
  if (!deviceWrapper) {
    return -1;
  }
  var error = await deviceWrapper.object.popErrorScope();
  return error ? 1 : 0;
});

// Verifies that a draw with the given uniform binding range is rejected by tgfx itself rather
// than by WebGPU. Three properties must hold:
// 1. Before the rejected draw, an independent green draw succeeds and reads back green — the
//    target never inherits a previous scenario's expected transparent color.
// 2. After the rejected draw, the target reads back transparent: the render pass still ran its
//    clear, proving the command buffer was executed and not silently discarded as a whole.
// 3. The validation error scope captured nothing — the rejection happened on the CPU before
//    any invalid binding reached WebGPU. If the range check regressed, WebGPU would reject the
//    bind group here and the scope would capture a validation error.
static void ExpectTgfxRejectedDraw(const UniformBindingTestEnv& env,
                                   const std::shared_ptr<GPUBuffer>& greenBuffer, size_t offset,
                                   size_t size, int surfaceSize) {
  EXPECT_TRUE(DrawQuadWithUniformBinding(env, greenBuffer, 0, 16));
  RGBA4f<AlphaType::Unpremultiplied> color = {};
  ASSERT_TRUE(ReadCenterPixel(env, surfaceSize, &color));
  ExpectColor(color, 0.0f, 1.0f, 0.0f, 1.0f);

  auto device = static_cast<WebGPUGPU*>(env.gpu)->device();
  wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);

  // Collect all results and pop the error scope before asserting anything, so that a failed
  // draw or readback cannot leak a pushed scope without its matching pop.
  auto drawSucceeded = DrawQuadWithUniformBinding(env, greenBuffer, offset, size);
  auto readSucceeded = ReadCenterPixel(env, surfaceSize, &color);
  auto scopeResult = WebGPUPopErrorScope(device);

  ASSERT_TRUE(drawSucceeded);
  ASSERT_TRUE(readSucceeded);
  ExpectColor(color, 0.0f, 0.0f, 0.0f, 0.0f);
  EXPECT_EQ(scopeResult, 0)
      << "binding offset=" << offset << " size=" << size
      << " must be rejected by tgfx without raising a WebGPU validation error";
}

// A uniform binding whose [offset, offset + size) range exceeds the buffer capacity must fail
// the bind group update and skip the draw, leaving the render target cleared. Clamping the
// binding size to the remaining capacity would turn an out-of-range binding (a caller struct
// layout bug) into a silently mis-sized valid binding.
TGFX_TEST(WebGPUUniformBindingTest, OutOfRangeUniformBindingSkipsDraw) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }

  constexpr int SIZE = 100;
  UniformBindingTestEnv env = {};
  ASSERT_TRUE(SetupUniformBindingTestEnv(&env, context, SIZE, UniformBlockKind::Vec4Color));

  // A valid 16-byte uniform: opaque green.
  struct Vec4Uniforms {
    float red = 0.0f;
    float green = 1.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
  };
  auto greenBuffer = env.gpu->createBuffer(sizeof(Vec4Uniforms), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(greenBuffer != nullptr);
  auto* greenData = static_cast<float*>(greenBuffer->map());
  ASSERT_TRUE(greenData != nullptr);
  greenData[0] = 0.0f;
  greenData[1] = 1.0f;
  greenData[2] = 0.0f;
  greenData[3] = 1.0f;
  greenBuffer->unmap();

  // offset = 0, size = 32 against a 16-byte buffer. A size clamp to the remaining capacity
  // would silently produce a valid 16-byte binding and draw green; the correct behavior is a
  // CPU-side rejection that skips the draw, leaving the target transparent.
  ExpectTgfxRejectedDraw(env, greenBuffer, 0, 32, SIZE);

  // offset + size wraps around on 32-bit size_t (wasm32): the raw addition equals 0 and must
  // not be mistaken for an in-range binding.
  ExpectTgfxRejectedDraw(env, greenBuffer, std::numeric_limits<size_t>::max(), 1, SIZE);

  // A zero-length binding is always invalid: a uniform block is at least 4 bytes.
  ExpectTgfxRejectedDraw(env, greenBuffer, 0, 0, SIZE);

  // Closing bookend: the valid binding still draws green after all the rejected ones, proving
  // no rejected scenario left the device or pipeline in a persistently broken state.
  EXPECT_TRUE(DrawQuadWithUniformBinding(env, greenBuffer, 0, 16));
  RGBA4f<AlphaType::Unpremultiplied> color = {};
  ASSERT_TRUE(ReadCenterPixel(env, SIZE, &color));
  ExpectColor(color, 0.0f, 1.0f, 0.0f, 1.0f);
}

// A `vec2 + float` std140 block is 16 bytes (the member layout rounds up to the vec2
// alignment), so a caller struct with only 12 bytes of members must be padded to 16 bytes
// before uploading and binding. This is the shape libpag's RadialBlurFilter uses; the test
// verifies the padded struct renders the correct per-member values.
TGFX_TEST(WebGPUUniformBindingTest, Vec2FloatBlockRendersWithTailPadding) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }

  constexpr int SIZE = 100;
  UniformBindingTestEnv env = {};
  ASSERT_TRUE(SetupUniformBindingTestEnv(&env, context, SIZE, UniformBlockKind::Vec2FloatTail));

  struct TailPaddedUniforms {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float amount = 0.0f;
    float padding = 0.0f;
  };
  static_assert(sizeof(TailPaddedUniforms) == 16,
                "Uniforms must match the std140 size of the Args block");

  TailPaddedUniforms uniforms = {};
  uniforms.centerX = 1.0f;
  uniforms.centerY = 0.5f;
  uniforms.amount = 0.25f;
  auto uniformBuffer = env.gpu->createBuffer(sizeof(TailPaddedUniforms), GPUBufferUsage::UNIFORM);
  ASSERT_TRUE(uniformBuffer != nullptr);
  auto* data = static_cast<float*>(uniformBuffer->map());
  ASSERT_TRUE(data != nullptr);
  memcpy(data, &uniforms, sizeof(TailPaddedUniforms));
  uniformBuffer->unmap();

  EXPECT_TRUE(DrawQuadWithUniformBinding(env, uniformBuffer, 0, sizeof(TailPaddedUniforms)));
  RGBA4f<AlphaType::Unpremultiplied> color = {};
  ASSERT_TRUE(ReadCenterPixel(env, SIZE, &color));
  ExpectColor(color, 1.0f, 0.5f, 0.25f, 1.0f);
}

// Declares a std140 uniform block plus two samplers, sampling the second one only behind a
// uniform branch. This is the shape libpag's single-input layer style filters use: the pipeline
// layout declares both samplers, and the guarded branch keeps any placeholder binding from
// ever being sampled.
static constexpr char OPTIONAL_SAMPLER_FRAGMENT_SHADER[] = R"(
    precision mediump float;
    in vec2 vertexColor;
    uniform sampler2D sTexture;
    uniform sampler2D sOriginalTexture;
    layout(std140) uniform Args {
        vec4 useOriginalTexture;
    };
    out vec4 tgfx_FragColor;
    void main() {
        vec4 color = texture(sTexture, vertexColor);
        if (useOriginalTexture.x > 0.5) {
            color = texture(sOriginalTexture, vertexColor);
        }
        tgfx_FragColor = color;
    }
)";

class OptionalSamplerEffect : public RuntimeEffect {
 public:
  explicit OptionalSamplerEffect(bool useOriginal,
                                 const std::vector<std::shared_ptr<Image>>& extraInputs = {})
      : RuntimeEffect(extraInputs), useOriginal(useOriginal) {
  }

  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point&) const override {
    if (inputTextures.empty() || inputTextures[0] == nullptr) {
      return false;
    }
    auto gpu = encoder->gpu();
    auto info = gpu->info();
    auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
    auto vertexModule = ShaderModuleDescriptor();
    vertexModule.code = PrefixShaderVersion(QUAD_VERTEX_SHADER, isDesktop);
    vertexModule.stage = ShaderStage::Vertex;
    auto vertexShader = gpu->createShaderModule(vertexModule);
    if (vertexShader == nullptr) {
      return false;
    }
    auto fragmentModule = ShaderModuleDescriptor();
    fragmentModule.code = PrefixShaderVersion(OPTIONAL_SAMPLER_FRAGMENT_SHADER, isDesktop);
    fragmentModule.stage = ShaderStage::Fragment;
    auto fragmentShader = gpu->createShaderModule(fragmentModule);
    if (fragmentShader == nullptr) {
      return false;
    }
    RenderPipelineDescriptor descriptor = {};
    descriptor.vertex.bufferLayouts = {VertexBufferLayout({{"aPosition", VertexFormat::Float2}},
                                                          VertexStepMode::Vertex)};
    descriptor.vertex.module = vertexShader;
    descriptor.fragment.module = fragmentShader;
    descriptor.fragment.colorAttachments.push_back({});
    descriptor.layout.uniformBlocks = {{"Args", 0, ShaderVisibility::Fragment}};
    descriptor.layout.textureSamplers = {{"sTexture", 0, ShaderVisibility::Fragment},
                                         {"sOriginalTexture", 1, ShaderVisibility::Fragment}};
    auto pipeline = gpu->createRenderPipeline(descriptor);
    if (pipeline == nullptr) {
      return false;
    }

    auto vertexBuffer = MakeQuadVertexBuffer(gpu);
    if (vertexBuffer == nullptr) {
      return false;
    }

    auto uniformBuffer = gpu->createBuffer(16, GPUBufferUsage::UNIFORM);
    if (uniformBuffer == nullptr) {
      return false;
    }
    auto* uniformData = static_cast<float*>(uniformBuffer->map());
    if (uniformData == nullptr) {
      return false;
    }
    memset(uniformData, 0, 16);
    uniformData[0] = useOriginal ? 1.0f : 0.0f;
    uniformBuffer->unmap();

    RenderPassDescriptor renderPassDesc(outputTexture, LoadAction::Clear, StoreAction::Store,
                                        PMColor::Transparent());
    auto renderPass = encoder->beginRenderPass(renderPassDesc);
    if (renderPass == nullptr) {
      return false;
    }
    renderPass->setPipeline(std::move(pipeline));
    renderPass->setVertexBuffer(0, vertexBuffer);
    SamplerDescriptor samplerDesc(AddressMode::ClampToEdge, AddressMode::ClampToEdge,
                                  FilterMode::Linear, FilterMode::Linear, MipmapMode::None);
    auto sampler = gpu->createSampler(samplerDesc);
    if (sampler == nullptr) {
      return false;
    }
    renderPass->setTexture(0, inputTextures[0], sampler);
    if (inputTextures.size() > 1 && inputTextures[1] != nullptr) {
      renderPass->setTexture(1, inputTextures[1], sampler);
    }
    // When no second input exists, binding 1 (sOriginalTexture) is intentionally left unbound;
    // the backend must complete the bind group with a placeholder instead of leaving the slot
    // missing.
    renderPass->setUniformBuffer(0, uniformBuffer, 0, uniformBuffer->size());
    renderPass->draw(PrimitiveType::TriangleStrip, 4);
    renderPass->end();
    return true;
  }

 private:
  bool useOriginal = false;
};

static void RunOptionalSamplerFilter(Context* context, int size, bool useOriginal,
                                     std::shared_ptr<Image> extraInput,
                                     RGBA4f<AlphaType::Unpremultiplied>* color) {
  static constexpr uint32_t GREEN = 0xFF00FF00;  // RGBA_8888 premultiplied green
  auto image = MakeSolidImage(size, GREEN);
  ASSERT_TRUE(image != nullptr);
  std::vector<std::shared_ptr<Image>> extraInputs = {};
  if (extraInput != nullptr) {
    extraInputs.push_back(std::move(extraInput));
  }
  auto effect = std::make_shared<OptionalSamplerEffect>(useOriginal, std::move(extraInputs));
  auto filter = ImageFilter::Runtime(std::move(effect));
  auto filtered = image->makeWithFilter(std::move(filter));
  ASSERT_TRUE(filtered != nullptr);

  auto surface = Surface::Make(context, size, size);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(filtered));

  Bitmap resultBitmap(size, size, false, false);
  Pixmap resultPixmap(resultBitmap);
  ASSERT_TRUE(surface->readPixels(resultPixmap.info(), resultPixmap.writablePixels()));
  *color = resultPixmap.getColor(size / 2, size / 2);
}

// A pipeline whose layout declares two texture samplers while the draw binds only one must have
// the unbound slots completed with placeholder bindings, mirroring GL's tolerant lazy binding.
// The guarded branch never samples the placeholder, so the output must equal the bound texture.
// The second scenario verifies the symmetrical case: when a real second input is provided and
// the branch is enabled, the output must come from that second texture, proving the placeholder
// path does not disturb genuinely bound slots.
TGFX_TEST(WebGPUUniformBindingTest, UnboundSamplerSlotFilledWithPlaceholder) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }

  constexpr int SIZE = 100;

  // No second input: the placeholder completes the bind group, the guarded branch is off, and
  // the output equals the first (green) texture.
  RGBA4f<AlphaType::Unpremultiplied> color = {};
  RunOptionalSamplerFilter(context, SIZE, false, nullptr, &color);
  ExpectColor(color, 0.0f, 1.0f, 0.0f, 1.0f);

  // Real second input with the branch enabled: the output must equal the second (blue) texture.
  static constexpr uint32_t BLUE = 0xFFFF0000;  // RGBA_8888 premultiplied blue
  auto blueImage = MakeSolidImage(SIZE, BLUE);
  ASSERT_TRUE(blueImage != nullptr);
  RunOptionalSamplerFilter(context, SIZE, true, std::move(blueImage), &color);
  ExpectColor(color, 0.0f, 0.0f, 1.0f, 1.0f);
}

}  // namespace tgfx
