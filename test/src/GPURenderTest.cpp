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
#include <vector>
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLUtil.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/RenderPass.h"
#include "utils/TestUtils.h"

namespace tgfx {

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

TGFX_TEST(GPURenderTest, GLVersion) {
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

TGFX_TEST(GPURenderTest, GLCaps) {
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

}  // namespace tgfx
