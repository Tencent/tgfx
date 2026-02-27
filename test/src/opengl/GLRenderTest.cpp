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

#include <vector>
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"
#include "tgfx/core/Canvas.h"
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
