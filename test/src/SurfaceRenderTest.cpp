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

#include "core/PathRef.h"
#include "core/PictureRecords.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/AppendShape.h"
#include "core/shapes/ProviderShape.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLDefines.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/gpu/opengl/GLTypes.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

namespace tgfx {

// ==================== Surface Tests ====================

TGFX_TEST(SurfaceRenderTest, ImageSnapshot) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 200;
  auto height = 200;
  auto texture = context->gpu()->createTexture({width, height, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  auto surface =
      Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::BottomLeft, 4);
  ASSERT_TRUE(surface != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(image);
  auto snapshotImage = surface->makeImageSnapshot();
  auto snapshotImage2 = surface->makeImageSnapshot();
  EXPECT_TRUE(snapshotImage == snapshotImage2);
  auto compareSurface = Surface::Make(context, width, height);
  auto compareCanvas = compareSurface->getCanvas();
  compareCanvas->drawImage(snapshotImage);
  EXPECT_TRUE(Baseline::Compare(compareSurface, "SurfaceRenderTest/ImageSnapshot1"));
  canvas->drawImage(image, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "SurfaceRenderTest/ImageSnapshot_Surface1"));
  compareCanvas->clear();
  compareCanvas->drawImage(snapshotImage);
  EXPECT_TRUE(Baseline::Compare(compareSurface, "SurfaceRenderTest/ImageSnapshot1"));

  surface = Surface::Make(context, width, height, false, 4);
  canvas = surface->getCanvas();
  snapshotImage = surface->makeImageSnapshot();
  auto renderTargetProxy = surface->renderContext->renderTarget;
  snapshotImage = nullptr;
  canvas->drawImage(image);
  context->flushAndSubmit();
  EXPECT_TRUE(renderTargetProxy == surface->renderContext->renderTarget);
  snapshotImage = surface->makeImageSnapshot();
  snapshotImage2 = surface->makeImageSnapshot();
  EXPECT_TRUE(snapshotImage == snapshotImage2);
  compareCanvas->clear();
  compareCanvas->drawImage(snapshotImage);
  EXPECT_TRUE(Baseline::Compare(compareSurface, "SurfaceRenderTest/ImageSnapshot2"));
  canvas->drawImage(image, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "SurfaceRenderTest/ImageSnapshot_Surface2"));
  compareCanvas->clear();
  compareCanvas->drawImage(snapshotImage);
  EXPECT_TRUE(Baseline::Compare(compareSurface, "SurfaceRenderTest/ImageSnapshot2"));
}

// ==================== Dst Texture Tests ====================

TGFX_TEST(SurfaceRenderTest, EmptyLocalBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 800;
  auto height = 600;
  auto renderTarget = RenderTarget::Make(context, width, height);
  auto backendRenderTarget = renderTarget->getBackendRenderTarget();

  auto surface = Surface::MakeFrom(context, backendRenderTarget, ImageOrigin::BottomLeft);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto paint = Paint();
  auto clipPath = Path();
  clipPath.addRect(Rect::MakeLTRB(0, 0, 400, 300));

  paint.setColor(Color::FromRGBA(0, 255, 0));
  canvas->drawPath(clipPath, paint);
  canvas->clipPath(clipPath);
  auto drawPath = Path();
  drawPath.addRoundRect(Rect::MakeLTRB(100, 100, 300, 250), 30.f, 30.f);
  paint.setColor(Color::FromRGBA(255, 0, 0));
  canvas->drawPath(drawPath, paint);
  auto matrix = Matrix::MakeTrans(750, 0);
  paint.setColor(Color::FromRGBA(0, 0, 255));
  drawPath.transform(matrix);
  paint.setBlendMode(BlendMode::SoftLight);
  canvas->drawPath(drawPath, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "SurfaceRenderTest/EmptyLocalBounds"));
}

TGFX_TEST(SurfaceRenderTest, OutOfRenderTarget) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 800;
  auto height = 600;
  auto renderTarget = RenderTarget::Make(context, width, height);
  auto backendRenderTarget = renderTarget->getBackendRenderTarget();

  auto surface = Surface::MakeFrom(context, backendRenderTarget, ImageOrigin::BottomLeft);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto paint = Paint();
  auto clipPath = Path();
  clipPath.addRect(Rect::MakeLTRB(0, 0, 400, 300));

  paint.setColor(Color::FromRGBA(0, 255, 0));
  canvas->drawPath(clipPath, paint);
  canvas->clipPath(clipPath);
  auto drawPath = Path();
  drawPath.addRect(Rect::MakeLTRB(100, 100, 300, 250));
  paint.setColor(Color::FromRGBA(255, 0, 0));
  canvas->drawPath(drawPath, paint);
  auto matrix = Matrix::MakeTrans(750, 0);
  paint.setColor(Color::FromRGBA(0, 0, 255));
  drawPath.transform(matrix);
  paint.setBlendMode(BlendMode::SoftLight);
  canvas->drawPath(drawPath, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "SurfaceRenderTest/OutOfRenderTarget"));
}

// Simple vertex shader for external rendering test (GLSL 3.00 ES).
static const char* ExternalVertexShaderES = R"(#version 300 es
  in vec2 aPosition;
  in vec3 aColor;
  out vec3 vColor;
  void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
  }
)";

// Simple fragment shader for external rendering test (GLSL 3.00 ES).
static const char* ExternalFragmentShaderES = R"(#version 300 es
  precision mediump float;
  in vec3 vColor;
  out vec4 fragColor;
  void main() {
    fragColor = vec4(vColor, 1.0);
  }
)";

// Simple vertex shader for external rendering test (GLSL 1.50 for desktop OpenGL).
static const char* ExternalVertexShaderGL = R"(#version 150
  in vec2 aPosition;
  in vec3 aColor;
  out vec3 vColor;
  void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
  }
)";

// Simple fragment shader for external rendering test (GLSL 1.50 for desktop OpenGL).
static const char* ExternalFragmentShaderGL = R"(#version 150
  in vec3 vColor;
  out vec4 fragColor;
  void main() {
    fragColor = vec4(vColor, 1.0);
  }
)";

// Compile a shader from source.
static unsigned CompileShader(const GLFunctions* gl, unsigned type, const char* source) {
  auto shader = gl->createShader(type);
  if (shader == 0) {
    return 0;
  }
  gl->shaderSource(shader, 1, &source, nullptr);
  gl->compileShader(shader);
  int status = 0;
  gl->getShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == 0) {
    gl->deleteShader(shader);
    return 0;
  }
  return shader;
}

// Create a shader program from vertex and fragment shaders.
static unsigned CreateExternalProgram(const GLFunctions* gl, bool isOpenGLES) {
  const char* vertexSource = isOpenGLES ? ExternalVertexShaderES : ExternalVertexShaderGL;
  const char* fragmentSource = isOpenGLES ? ExternalFragmentShaderES : ExternalFragmentShaderGL;
  auto vertexShader = CompileShader(gl, GL_VERTEX_SHADER, vertexSource);
  if (vertexShader == 0) {
    return 0;
  }
  auto fragmentShader = CompileShader(gl, GL_FRAGMENT_SHADER, fragmentSource);
  if (fragmentShader == 0) {
    gl->deleteShader(vertexShader);
    return 0;
  }
  auto program = gl->createProgram();
  if (program == 0) {
    gl->deleteShader(vertexShader);
    gl->deleteShader(fragmentShader);
    return 0;
  }
  gl->attachShader(program, vertexShader);
  gl->attachShader(program, fragmentShader);
  gl->linkProgram(program);
  gl->deleteShader(vertexShader);
  gl->deleteShader(fragmentShader);
  int status = 0;
  gl->getProgramiv(program, GL_LINK_STATUS, &status);
  if (status == 0) {
    gl->deleteProgram(program);
    return 0;
  }
  return program;
}

// Create external GL texture for testing.
static GLTextureInfo CreateExternalTexture(const GLFunctions* gl, int width, int height) {
  GLTextureInfo glInfo = {};
  gl->genTextures(1, &glInfo.id);
  if (glInfo.id == 0) {
    return {};
  }
  glInfo.target = GL_TEXTURE_2D;
  glInfo.format = GL_RGBA8;
  gl->bindTexture(glInfo.target, glInfo.id);
  gl->texParameteri(glInfo.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(glInfo.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->texParameteri(glInfo.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(glInfo.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texImage2D(glInfo.target, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  return glInfo;
}

/**
 * Test that tgfx correctly saves and restores external GL state when rendering to a shared texture.
 *
 * Test flow:
 * 1. External creates GL resources (texture, FBO, VAO, VBO, shader program)
 * 2. External renders a green triangle
 * 3. Use tgfx to draw multiple complex graphics (gradients, paths, image, blend modes, etc.)
 * 4. Verify external GL state is restored correctly
 * 5. External renders a blue triangle (relies on restored GL state)
 * 6. Verify final rendering result
 */
TGFX_TEST(SurfaceRenderTest, GLStateRestore) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto gl = gpu->functions();
  ASSERT_TRUE(gl->genVertexArrays != nullptr);

  // Determine if we're using OpenGL ES based on version string.
  auto gpuInfo = gpu->info();
  bool isOpenGLES = gpuInfo->version.find("OpenGL ES") != std::string::npos;

  // Step 1: Create external GL resources.
  constexpr int textureWidth = 200;
  constexpr int textureHeight = 200;
  auto glInfo = CreateExternalTexture(gl, textureWidth, textureHeight);
  ASSERT_TRUE(glInfo.id > 0);

  unsigned externalFBO = 0;
  gl->genFramebuffers(1, &externalFBO);
  ASSERT_TRUE(externalFBO > 0);
  gl->bindFramebuffer(GL_FRAMEBUFFER, externalFBO);
  gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glInfo.id, 0);

  auto externalProgram = CreateExternalProgram(gl, isOpenGLES);
  ASSERT_TRUE(externalProgram > 0);

  // clang-format off
  float vertices[] = {
    // Position      // Color (Green triangle)
    -0.5f, -0.5f,    0.0f, 1.0f, 0.0f,
     0.5f, -0.5f,    0.0f, 1.0f, 0.0f,
     0.0f,  0.5f,    0.0f, 1.0f, 0.0f,
  };
  // clang-format on

  unsigned externalVAO = 0;
  gl->genVertexArrays(1, &externalVAO);
  ASSERT_TRUE(externalVAO > 0);

  unsigned externalVBO = 0;
  gl->genBuffers(1, &externalVBO);
  ASSERT_TRUE(externalVBO > 0);

  gl->bindVertexArray(externalVAO);
  gl->bindBuffer(GL_ARRAY_BUFFER, externalVBO);
  gl->bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  auto positionLoc = gl->getAttribLocation(externalProgram, "aPosition");
  auto colorLoc = gl->getAttribLocation(externalProgram, "aColor");
  gl->enableVertexAttribArray(static_cast<unsigned>(positionLoc));
  gl->vertexAttribPointer(static_cast<unsigned>(positionLoc), 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), nullptr);
  gl->enableVertexAttribArray(static_cast<unsigned>(colorLoc));
  gl->vertexAttribPointer(static_cast<unsigned>(colorLoc), 3, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

  // Step 2: First external render - draw green triangle on white background.
  gl->viewport(0, 0, textureWidth, textureHeight);
  gl->disable(GL_SCISSOR_TEST);
  gl->disable(GL_BLEND);
  gl->clearColor(1.0f, 1.0f, 1.0f, 1.0f);
  gl->clear(GL_COLOR_BUFFER_BIT);
  gl->useProgram(externalProgram);
  gl->drawArrays(GL_TRIANGLES, 0, 3);

  // Step 3: Use tgfx to render multiple complex graphics.
  BackendTexture backendTexture(glInfo, textureWidth, textureHeight);
  auto surface = Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();

  // 3.1: Draw a red rectangle (solid color).
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeXYWH(130, 10, 60, 40), paint);

  // 3.2: Draw a rounded rectangle with gradient.
  auto yellow = Color::FromRGBA(255, 255, 0);
  auto magenta = Color::FromRGBA(255, 0, 255);
  auto shader = Shader::MakeLinearGradient({10, 60}, {90, 110}, {yellow, magenta}, {});
  paint.setShader(shader);
  Path roundRectPath = {};
  roundRectPath.addRoundRect(Rect::MakeXYWH(10, 60, 80, 50), 10, 10);
  canvas->drawPath(roundRectPath, paint);

  // 3.3: Draw an ellipse with radial gradient.
  auto cyan = Color::FromRGBA(0, 255, 255);
  shader = Shader::MakeRadialGradient({155, 85}, 30, {cyan, Color::Blue()}, {});
  paint.setShader(shader);
  Path ellipsePath = {};
  ellipsePath.addOval(Rect::MakeXYWH(120, 55, 70, 60));
  canvas->drawPath(ellipsePath, paint);

  // 3.4: Draw a star path with blend mode.
  paint.setShader(nullptr);
  paint.setColor(Color::FromRGBA(255, 165, 0, 200));
  paint.setBlendMode(BlendMode::Multiply);
  Path starPath = {};
  float cx = 50;
  float cy = 150;
  float outerRadius = 30;
  float innerRadius = 12;
  for (int i = 0; i < 5; i++) {
    float outerAngle = static_cast<float>(i) * 72.0f - 90.0f;
    float innerAngle = outerAngle + 36.0f;
    float outerX = cx + outerRadius * cosf(outerAngle * static_cast<float>(M_PI) / 180.0f);
    float outerY = cy + outerRadius * sinf(outerAngle * static_cast<float>(M_PI) / 180.0f);
    float innerX = cx + innerRadius * cosf(innerAngle * static_cast<float>(M_PI) / 180.0f);
    float innerY = cy + innerRadius * sinf(innerAngle * static_cast<float>(M_PI) / 180.0f);
    if (i == 0) {
      starPath.moveTo(outerX, outerY);
    } else {
      starPath.lineTo(outerX, outerY);
    }
    starPath.lineTo(innerX, innerY);
  }
  starPath.close();
  canvas->drawPath(starPath, paint);

  // 3.5: Draw a bezier curve path.
  paint.setBlendMode(BlendMode::SrcOver);
  paint.setColor(Color::FromRGBA(128, 0, 128));
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(3);
  Path bezierPath = {};
  bezierPath.moveTo(100, 130);
  bezierPath.cubicTo(120, 100, 160, 180, 190, 140);
  canvas->drawPath(bezierPath, paint);

  // 3.6: Draw an image with clip.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  if (image != nullptr) {
    canvas->save();
    Path clipPath = {};
    clipPath.addOval(Rect::MakeXYWH(120, 140, 60, 50));
    canvas->clipPath(clipPath);
    paint.setStyle(PaintStyle::Fill);
    paint.setShader(nullptr);
    auto imageMatrix = Matrix::MakeScale(0.3f);
    imageMatrix.postTranslate(110, 130);
    canvas->setMatrix(imageMatrix);
    canvas->drawImage(image);
    canvas->restore();
  }

  context->flushAndSubmit();

  // Step 4: Verify external GL state is restored correctly.
  int actualViewport[4] = {0};
  gl->getIntegerv(GL_VIEWPORT, actualViewport);
  EXPECT_EQ(actualViewport[0], 0);
  EXPECT_EQ(actualViewport[1], 0);
  EXPECT_EQ(actualViewport[2], textureWidth);
  EXPECT_EQ(actualViewport[3], textureHeight);

  unsigned char scissorEnabled = GL_TRUE;
  gl->getBooleanv(GL_SCISSOR_TEST, &scissorEnabled);
  EXPECT_EQ(scissorEnabled, GL_FALSE);

  unsigned char blendEnabled = GL_TRUE;
  gl->getBooleanv(GL_BLEND, &blendEnabled);
  EXPECT_EQ(blendEnabled, GL_FALSE);

  int actualProgram = 0;
  gl->getIntegerv(GL_CURRENT_PROGRAM, &actualProgram);
  EXPECT_EQ(static_cast<unsigned>(actualProgram), externalProgram);

  int actualFBO = 0;
  gl->getIntegerv(GL_FRAMEBUFFER_BINDING, &actualFBO);
  EXPECT_EQ(static_cast<unsigned>(actualFBO), externalFBO);

  int actualVAO = 0;
  gl->getIntegerv(GL_VERTEX_ARRAY_BINDING, &actualVAO);
  EXPECT_EQ(static_cast<unsigned>(actualVAO), externalVAO);

  // Step 5: Second external render - draw blue triangle.
  // External code assumes GL state is preserved, so it only updates the vertex data.
  // clang-format off
  float vertices2[] = {
    // Position      // Color (Blue triangle)
    -0.5f,  0.0f,    0.0f, 0.0f, 1.0f,
     0.5f,  0.0f,    0.0f, 0.0f, 1.0f,
     0.0f, -0.8f,    0.0f, 0.0f, 1.0f,
  };
  // clang-format on

  // Need to rebind VBO to update data (VAO stores attribute pointers, not current buffer binding).
  gl->bindBuffer(GL_ARRAY_BUFFER, externalVBO);
  gl->bufferData(GL_ARRAY_BUFFER, sizeof(vertices2), vertices2, GL_STATIC_DRAW);
  gl->drawArrays(GL_TRIANGLES, 0, 3);
  gl->flush();

  // Step 6: Verify the final rendering result.
  // Expected: white background + green triangle + tgfx graphics + blue triangle
  EXPECT_TRUE(Baseline::Compare(surface, "SurfaceRenderTest/GLStateRestore"));

  // Cleanup.
  gl->deleteProgram(externalProgram);
  gl->deleteBuffers(1, &externalVBO);
  gl->deleteVertexArrays(1, &externalVAO);
  gl->deleteFramebuffers(1, &externalFBO);
  gl->deleteTextures(1, &glInfo.id);
}

}  // namespace tgfx
