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

#include <array>
#include <chrono>
#include <vector>
#include "core/utils/BlockAllocator.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLFunctions.h"
#include <CoreVideo/CoreVideo.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLContext.h>
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

// ==================== GL Utility Tests ====================

static size_t vendorIndex = 0;

std::vector<std::pair<std::string, GLVendor>> vendors = {
    {"ATI Technologies Inc.", GLVendor::ATI},
    {"ARM", GLVendor::ARM},
    {"NVIDIA Corporation", GLVendor::NVIDIA},
    {"Qualcomm", GLVendor::Qualcomm},
    {"Intel", GLVendor::Intel},
    {"Imagination Technologies", GLVendor::Imagination},
};
const unsigned char* glGetStringMock(unsigned name) {
  if (name == GL_VENDOR) {
    return reinterpret_cast<const unsigned char*>(vendors[vendorIndex].first.c_str());
  } else if (name == GL_VERSION) {
    if (vendorIndex != 0) {
      return reinterpret_cast<const unsigned char*>("3.2");
    } else {
      return reinterpret_cast<const unsigned char*>("5.0");
    }
  }
  return nullptr;
}

void getIntegervMock(unsigned pname, int* params) {
  if (pname == GL_MAX_TEXTURE_SIZE) {
    *params = 1024;
  }
}

void glGetInternalformativMock(unsigned target, unsigned, unsigned pname, int, int* params) {
  if (target != GL_RENDERBUFFER) {
    return;
  }
  if (pname == GL_NUM_SAMPLE_COUNTS) {
    *params = 2;
    return;
  }
  if (pname == GL_SAMPLES) {
    params[0] = 8;
    params[1] = 4;
  }
}

void glGetShaderPrecisionFormatMock(unsigned, unsigned, int* range, int* precision) {
  range[0] = 127;
  range[1] = 127;
  *precision = 32;
}

TGFX_TEST(GLRenderTest, GLVersion) {
  auto glVersion = GetGLVersion(nullptr);
  EXPECT_EQ(glVersion.majorVersion, -1);
  EXPECT_EQ(glVersion.minorVersion, -1);
  glVersion = GetGLVersion("");
  EXPECT_EQ(glVersion.majorVersion, -1);
  EXPECT_EQ(glVersion.minorVersion, -1);
  glVersion = GetGLVersion("2.1 Mesa 10.1.1");
  EXPECT_EQ(glVersion.majorVersion, 2);
  EXPECT_EQ(glVersion.minorVersion, 1);
  glVersion = GetGLVersion("3.1");
  EXPECT_EQ(glVersion.majorVersion, 3);
  EXPECT_EQ(glVersion.minorVersion, 1);
  glVersion = GetGLVersion("OpenGL ES 2.0 (WebGL 1.0 (OpenGL ES 2.0 Chromium))");
  EXPECT_EQ(glVersion.majorVersion, 1);
  EXPECT_EQ(glVersion.minorVersion, 0);
  glVersion = GetGLVersion("OpenGL ES-CM 1.1 Apple A8 GPU - 50.5.1");
  EXPECT_EQ(glVersion.majorVersion, 1);
  EXPECT_EQ(glVersion.minorVersion, 1);
  glVersion = GetGLVersion("OpenGL ES 2.0 Apple A8 GPU - 50.5.1");
  EXPECT_EQ(glVersion.majorVersion, 2);
  EXPECT_EQ(glVersion.minorVersion, 0);
}

TGFX_TEST(GLRenderTest, GLCaps) {
  {
    GLInfo info(glGetStringMock, nullptr, getIntegervMock, glGetInternalformativMock,
                glGetShaderPrecisionFormatMock);
    GLCaps caps(info);
    EXPECT_EQ(caps.vendor, vendors[vendorIndex].second);
    EXPECT_EQ(caps.standard, GLStandard::GL);
    EXPECT_TRUE(caps.multisampleDisableSupport);
    EXPECT_EQ(caps.getSampleCount(5, PixelFormat::RGBA_8888), 8);
    EXPECT_EQ(caps.getSampleCount(10, PixelFormat::RGBA_8888), 1);
    EXPECT_EQ(caps.getSampleCount(0, PixelFormat::RGBA_8888), 1);
    EXPECT_EQ(caps.getSampleCount(5, PixelFormat::ALPHA_8), 8);
  }
  {
    vendorIndex++;
    for (; vendorIndex < vendors.size(); ++vendorIndex) {
      GLInfo info(glGetStringMock, nullptr, getIntegervMock, glGetInternalformativMock,
                  glGetShaderPrecisionFormatMock);
      GLCaps caps(info);
      EXPECT_EQ(caps.vendor, vendors[vendorIndex].second);
    }
  }
}

// ==================== GL Image Tests ====================

static GLTextureInfo CreateRectangleTexture(Context* context, int width, int height) {
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  GLTextureInfo glInfo = {};
  gl->genTextures(1, &(glInfo.id));
  if (glInfo.id == 0) {
    return {};
  }
  glInfo.target = GL_TEXTURE_RECTANGLE;
  gl->bindTexture(glInfo.target, glInfo.id);
  auto gpu = static_cast<GLGPU*>(context->gpu());
  const auto& textureFormat = gpu->caps()->getTextureFormat(PixelFormat::RGBA_8888);
  gl->texImage2D(glInfo.target, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                 height, 0, textureFormat.externalFormat, textureFormat.externalType, nullptr);
  return glInfo;
}

// Measures the two image import paths on desktop OpenGL: the IOSurface zero-copy import
// (CVOpenGLTextureCache, produces GL_TEXTURE_RECTANGLE) versus a plain GL_TEXTURE_2D upload
// (genTextures + texImage2D + glFinish). Each round creates and releases a fresh texture like a
// real image decode would; the pixel source is a single pre-filled buffer so only the import cost
// is measured.
TGFX_TEST(GLRenderTest, TextureImportBenchmark) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(context->backend(), Backend::OpenGL);
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  const std::pair<int, int> sizes[] = {{512, 512}, {1080, 1920}};
  const int rounds = 50;
  for (const auto& size : sizes) {
    const int width = size.first;
    const int height = size.second;
    auto byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    auto pixels = std::vector<uint8_t>(byteCount, 0x7f);
    // Zero-copy path: CVPixelBuffer (IOSurface) -> CVOpenGLTextureCache -> RECTANGLE texture.
    CVPixelBufferRef pixelBuffer = nullptr;
    // An empty IOSurface properties dictionary mirrors HardwareBuffer.mm: the pixel buffer
    // allocates with a default IOSurface backing, which is what the zero-copy import needs.
    auto emptySurfaceProps =
        CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* optionKeys[] = {kCVPixelBufferIOSurfacePropertiesKey};
    const void* optionValues[] = {emptySurfaceProps};
    auto options =
        CFDictionaryCreate(kCFAllocatorDefault, optionKeys, optionValues, 1,
                           &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(emptySurfaceProps);
    EXPECT_EQ(static_cast<int>(CVPixelBufferCreate(kCFAllocatorDefault, static_cast<size_t>(width),
                                                static_cast<size_t>(height),
                                                kCVPixelFormatType_32BGRA, options, &pixelBuffer)),
              static_cast<int>(kCVReturnSuccess));
    CFRelease(options);
    CVOpenGLTextureCacheRef textureCache = nullptr;
    auto cglContext = CGLGetCurrentContext();
    ASSERT_NE(cglContext, nullptr);
    auto cglPixelFormat = CGLGetPixelFormat(cglContext);
    ASSERT_NE(cglPixelFormat, nullptr);
    EXPECT_EQ(static_cast<int>(CVOpenGLTextureCacheCreate(
                  kCFAllocatorDefault, nullptr, cglContext, cglPixelFormat, nullptr,
                  &textureCache)),
              static_cast<int>(kCVReturnSuccess));
    CVOpenGLTextureRef adopted = nullptr;
    // Warm up both paths once so first-touch allocations do not skew the averages.
    EXPECT_EQ(static_cast<int>(CVOpenGLTextureCacheCreateTextureFromImage(
                  kCFAllocatorDefault, textureCache, pixelBuffer, nullptr, &adopted)),
              static_cast<int>(kCVReturnSuccess));
    CVOpenGLTextureRelease(adopted);
    unsigned warmup = 0;
    gl->genTextures(1, &warmup);
    gl->bindTexture(GL_TEXTURE_2D, warmup);
    const auto& format = static_cast<GLGPU*>(context->gpu())
                             ->caps()
                             ->getTextureFormat(PixelFormat::RGBA_8888);
    gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(format.internalFormatTexImage), width, height,
                   0, format.externalFormat, format.externalType, pixels.data());
    gl->deleteTextures(1, &warmup);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      CVOpenGLTextureRef texture = nullptr;
      CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache, pixelBuffer,
                                                 nullptr, &texture);
      CVOpenGLTextureRelease(texture);
    }
    gl->finish();
    auto zeroCopyMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
        rounds;

    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      unsigned id = 0;
      gl->genTextures(1, &id);
      gl->bindTexture(GL_TEXTURE_2D, id);
      gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(format.internalFormatTexImage), width,
                     height, 0, format.externalFormat, format.externalType, pixels.data());
      gl->deleteTextures(1, &id);
    }
    gl->finish();
    auto uploadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
        rounds;

    // BGRA is the native Apple GPU layout; this variant isolates how much of the RGBA upload cost
    // is the driver's CPU-side format conversion rather than the transfer itself.
    const auto& bgraFormat =
        static_cast<GLGPU*>(context->gpu())->caps()->getTextureFormat(PixelFormat::BGRA_8888);
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      unsigned id = 0;
      gl->genTextures(1, &id);
      gl->bindTexture(GL_TEXTURE_2D, id);
      gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(bgraFormat.internalFormatTexImage), width,
                     height, 0, bgraFormat.externalFormat, bgraFormat.externalType, pixels.data());
      gl->deleteTextures(1, &id);
    }
    gl->finish();
    auto uploadBgraMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
        rounds;

    // Same upload without the per-round glFinish: measures the CPU-side submission cost only,
    // separating the driver's synchronous transfer work from the deferred GPU work.
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; ++i) {
      unsigned id = 0;
      gl->genTextures(1, &id);
      gl->bindTexture(GL_TEXTURE_2D, id);
      gl->texImage2D(GL_TEXTURE_2D, 0, static_cast<int>(format.internalFormatTexImage), width,
                     height, 0, format.externalFormat, format.externalType, pixels.data());
      gl->deleteTextures(1, &id);
    }
    auto submitOnlyMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
        rounds;
    gl->finish();

    LOGI("[Benchmark] %dx%d zeroCopyImport=%.3fms uploadRGBA=%.3fms uploadBGRA=%.3fms "
         "submitOnly=%.3fms",
         width, height, zeroCopyMs, uploadMs, uploadBgraMs, submitOnlyMs);
    CVOpenGLTextureCacheRelease(textureCache);
    CVPixelBufferRelease(pixelBuffer);
  }
}

TGFX_TEST(GLRenderTest, AOTWhitelist) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(context->backend(), Backend::OpenGL);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  {
    BlockAllocator allocator;
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::None, PMColor::White(),
                                                   Matrix::I(), false);
    auto textureProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    auto texture = TextureEffect::Make(&allocator, std::move(textureProxy));
    std::array<float, 20> matrixValues = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    auto matrix = ColorMatrixFragmentProcessor::Make(&allocator, matrixValues);
    auto fp = FragmentProcessor::Compose(&allocator, std::move(texture), std::move(matrix));
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(fp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {fp.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    EXPECT_TRUE(programInfo.usesOpenGLDesktopAOTProfile());
    EXPECT_TRUE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::None, PMColor::White(),
                                                   Matrix::I(), false);
    auto textureProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    auto fp = TextureEffect::Make(&allocator, std::move(textureProxy));
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(fp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {fp.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    // QuadPerEdgeAA + TextureEffect is served by QuadTextureFillShader.
    EXPECT_TRUE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    // Adopting an external texture caches its view under a scratch key derived from size/format.
    // An unusual size keeps the adopted rectangle view from being recycled by the plain 2x2
    // proxies other blocks (and other tests on the shared device) create, which would make their
    // matched texture kind depend on test order.
    auto glInfo = CreateRectangleTexture(context, 7, 5);
    ASSERT_NE(glInfo.id, 0u);
    auto textureProxy = context->proxyProvider()->wrapExternalTexture(BackendTexture(glInfo, 7, 5),
                                                                      ImageOrigin::TopLeft, true);
    auto fp = DeviceSpaceTextureEffect::Make(&allocator, std::move(textureProxy), Matrix::I());
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(fp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {fp.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    EXPECT_FALSE(programInfo.samplersAre2D());
    // A rectangle texture (external adopters only) has no precompiled variant and is rejected by
    // the matcher gate (it would otherwise bind to a plain sampler2D).
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::None, PMColor::White(),
                                                   Matrix::I(), false);
    auto textureProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    auto texture = TextureEffect::Make(&allocator, std::move(textureProxy));
    std::array<float, 20> matrixValues = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    auto matrix = ColorMatrixFragmentProcessor::Make(&allocator, matrixValues);
    auto fp = FragmentProcessor::Compose(&allocator, std::move(texture), std::move(matrix));
    auto xp = PorterDuffXferProcessor::Make(&allocator, BlendMode::SrcOver, {});
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(fp, nullptr);
    ASSERT_NE(xp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {fp.get()}, 1, xp.get(),
                            BlendMode::SrcOver);
    // SrcOver needs no dst readback, so the chain kernel serves this draw.
    EXPECT_TRUE(MatchPermutation(&programInfo).has_value());
  }
}

TGFX_TEST(GLRenderTest, InvalidShaderReturnsNull) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  ShaderModuleDescriptor descriptor = {};
  descriptor.stage = ShaderStage::Vertex;
  descriptor.code = "#version 150\ninvalid shader";
  EXPECT_EQ(context->gpu()->createShaderModule(descriptor), nullptr);
}

TGFX_TEST(GLRenderTest, EmbeddedAOTCreatesPipeline) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto cache = context->precompiledShaderCache();
  cache->unload();
  auto sourceSurface = Surface::Make(context, 8, 8, false, 1, true);
  ASSERT_NE(sourceSurface, nullptr);
  sourceSurface->getCanvas()->clear(Color::Red());
  auto sourceImage = sourceSurface->makeImageSnapshot();
  ASSERT_NE(sourceImage, nullptr);
  context->flushAndSubmit(true);

  auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
  ASSERT_NE(bundleData, nullptr);
  ASSERT_GT(bundleSize, 0u);
  ASSERT_TRUE(cache->loadBundle(bundleData, bundleSize));
  EXPECT_EQ(cache->profileTag(), "opengl");
  cache->resetStats();

  auto surface = Surface::Make(context, 8, 8);
  ASSERT_NE(surface, nullptr);
  std::array<float, 20> matrix = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  Paint paint;
  paint.setColorFilter(ColorFilter::Matrix(matrix));
  surface->getCanvas()->drawImage(sourceImage, 0, 0, &paint);
  context->flushAndSubmit(true);

  EXPECT_GT(cache->hitCount(), 0u);
  EXPECT_EQ(cache->missCount(), 0u);
  EXPECT_GT(cache->aotStageCount(PrecompiledAOTStage::VertexModuleCreated), 0u);
  EXPECT_GT(cache->aotStageCount(PrecompiledAOTStage::FragmentModuleCreated), 0u);
  EXPECT_GT(cache->aotStageCount(PrecompiledAOTStage::PipelineCreated), 0u);
}

TGFX_TEST(GLRenderTest, TileModeFallback) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false, codec->colorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  auto pixels = bitmap.lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  auto result = codec->readPixels(bitmap.info(), pixels);
  ASSERT_TRUE(result);
  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto gl = gpu->functions();
  GLTextureInfo glInfo = CreateRectangleTexture(context, bitmap.width(), bitmap.height());
  ASSERT_TRUE(glInfo.id != 0);
  const auto& textureFormat =
      gpu->caps()->getTextureFormat(ColorTypeToPixelFormat(bitmap.colorType()));
  gl->texImage2D(glInfo.target, 0, static_cast<int>(textureFormat.internalFormatTexImage),
                 bitmap.width(), bitmap.height(), 0, textureFormat.externalFormat,
                 textureFormat.externalType, pixels);
  bitmap.unlockPixels();
  BackendTexture backendTexture(glInfo, bitmap.width(), bitmap.height());
  auto image = Image::MakeFrom(context, backendTexture, ImageOrigin::TopLeft, bitmap.colorSpace());
  ASSERT_TRUE(image != nullptr);
  image = image->makeOriented(codec->orientation());
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 2, image->height() / 2);
  auto canvas = surface->getCanvas();
  Paint paint;
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Nearest);
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror, sampling)
                    ->makeWithMatrix(Matrix::MakeScale(0.125f));
  paint.setShader(shader);
  canvas->translate(100, 100);
  auto drawRect = Rect::MakeXYWH(0, 0, surface->width() - 200, surface->height() - 200);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/TileModeFallback"));
  gl->deleteTextures(1, &glInfo.id);
}

TGFX_TEST(GLRenderTest, rectangleTextureAsBlendDst) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto glInfo = CreateRectangleTexture(context, 110, 110);
  ASSERT_TRUE(glInfo.id > 0);
  auto backendTexture = BackendTexture(glInfo, 110, 110);
  auto surface = Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft, 4);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  canvas->drawImage(image);
  image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  Paint paint = {};
  paint.setBlendMode(BlendMode::Multiply);
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/hardware_render_target_blend"));
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->deleteTextures(1, &(glInfo.id));
}

}  // namespace tgfx
