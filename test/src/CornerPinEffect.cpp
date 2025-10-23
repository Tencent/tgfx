/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "CornerPinEffect.h"
#include <string>
#include "gpu/GPU.h"

namespace tgfx {
static constexpr char CORNER_PIN_VERTEX_SHADER[] = R"(
        in vec2 aPosition;
        in vec3 aTextureCoord;
        out vec3 vertexColor;
        void main() {
            vec3 position = vec3(aPosition, 1);
            gl_Position = vec4(position.xy, 0, 1);
            vertexColor = aTextureCoord;
        }
    )";

static constexpr char CORNER_PIN_FRAGMENT_SHADER[] = R"(
        precision mediump float;
        in vec3 vertexColor;
        uniform sampler2D sTexture;
        out vec4 tgfx_FragColor;
        void main() {
            tgfx_FragColor = texture(sTexture, vertexColor.xy / vertexColor.z);
        }
    )";

static std::string GetFinalShaderCode(const char* codeSnippet, bool isDesktop) {
  if (isDesktop) {
    return std::string("#version 140\n\n") + codeSnippet;
  } else {
    return std::string("#version 300 es\n\n") + codeSnippet;
  }
}

std::shared_ptr<CornerPinEffect> CornerPinEffect::Make(const Point& upperLeft,
                                                       const Point& upperRight,
                                                       const Point& lowerRight,
                                                       const Point& lowerLeft) {
  return std::make_shared<CornerPinEffect>(upperLeft, upperRight, lowerRight, lowerLeft);
}

CornerPinEffect::CornerPinEffect(const Point& upperLeft, const Point& upperRight,
                                 const Point& lowerRight, const Point& lowerLeft) {
  cornerPoints[0] = lowerLeft;
  cornerPoints[1] = lowerRight;
  cornerPoints[2] = upperLeft;
  cornerPoints[3] = upperRight;
  calculateVertexQs();
}

Rect CornerPinEffect::filterBounds(const Rect&, MapDirection mapDirection) const {
  if (mapDirection == Reverse) {
    const auto largeSize = static_cast<float>(1 << 29);
    return Rect::MakeLTRB(-largeSize, -largeSize, largeSize, largeSize);
  }
  auto& lowerLeft = cornerPoints[0];
  auto& lowerRight = cornerPoints[1];
  auto& upperLeft = cornerPoints[2];
  auto& upperRight = cornerPoints[3];
  auto left = std::min(std::min(upperLeft.x, lowerLeft.x), std::min(upperRight.x, lowerRight.x));
  auto top = std::min(std::min(upperLeft.y, lowerLeft.y), std::min(upperRight.y, lowerRight.y));
  auto right = std::max(std::max(upperLeft.x, lowerLeft.x), std::max(upperRight.x, lowerRight.x));
  auto bottom = std::max(std::max(upperLeft.y, lowerLeft.y), std::max(upperRight.y, lowerRight.y));
  return Rect::MakeLTRB(left, top, right, bottom);
}

std::unique_ptr<RuntimeProgram> CornerPinEffect::onCreateProgram(Context* context) const {
  auto gl = GLFunctions::Get(context);
  // Clear the previously generated GLError, causing the subsequent CheckGLError to return an
  // incorrect result.
  ClearGLError(gl);
  auto info = context->gpu()->info();
  auto isDesktop = info->version.find("OpenGL ES") == std::string::npos;
  auto filterProgram =
      FilterProgram::Make(context, GetFinalShaderCode(CORNER_PIN_VERTEX_SHADER, isDesktop),
                          GetFinalShaderCode(CORNER_PIN_FRAGMENT_SHADER, isDesktop));
  if (filterProgram == nullptr) {
    return nullptr;
  }

  auto program = filterProgram->program;
  auto uniforms = std::make_unique<CornerPinUniforms>();
  uniforms->positionHandle = gl->getAttribLocation(program, "aPosition");
  uniforms->textureCoordHandle = gl->getAttribLocation(program, "aTextureCoord");
  filterProgram->uniforms = std::move(uniforms);
  if (!CheckGLError(gl)) {
    return nullptr;
  }
  return filterProgram;
}

bool CornerPinEffect::onDraw(const RuntimeProgram* program,
                             const std::vector<BackendTexture>& inputTextures,
                             const BackendRenderTarget& target, const Point& offset) const {
  auto context = program->getContext();
  auto gl = tgfx::GLFunctions::Get(context);
  // Clear the previously generated GLError
  ClearGLError(gl);
  auto filterProgram = static_cast<const FilterProgram*>(program);
  auto uniforms = static_cast<const CornerPinUniforms*>(filterProgram->uniforms.get());
  if (sampleCount() > 1) {
    gl->enable(GL_MULTISAMPLE);
  }
  gl->useProgram(filterProgram->program);
  gl->disable(GL_SCISSOR_TEST);
  gl->enable(GL_BLEND);
  gl->blendEquation(GL_FUNC_ADD);
  gl->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  GLFrameBufferInfo frameBuffer = {};
  target.getGLFramebufferInfo(&frameBuffer);
  gl->bindFramebuffer(GL_FRAMEBUFFER, frameBuffer.id);
  gl->viewport(0, 0, target.width(), target.height());
  GLTextureInfo glInfo = {};
  inputTextures[0].getGLTextureInfo(&glInfo);
  gl->activeTexture(GL_TEXTURE0);
  gl->bindTexture(glInfo.target, glInfo.id);
  gl->texParameteri(glInfo.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(glInfo.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(glInfo.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(glInfo.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  auto vertices = computeVertices(inputTextures[0], target, offset);
  if (filterProgram->vertexArray > 0) {
    gl->bindVertexArray(filterProgram->vertexArray);
  }
  gl->bindBuffer(GL_ARRAY_BUFFER, filterProgram->vertexBuffer);
  gl->bufferData(GL_ARRAY_BUFFER, static_cast<long>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STREAM_DRAW);
  gl->vertexAttribPointer(static_cast<unsigned>(uniforms->positionHandle), 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), static_cast<void*>(0));
  gl->enableVertexAttribArray(static_cast<unsigned>(uniforms->positionHandle));

  gl->vertexAttribPointer(static_cast<unsigned>(uniforms->textureCoordHandle), 3, GL_FLOAT,
                          GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
  gl->enableVertexAttribArray(static_cast<unsigned>(uniforms->textureCoordHandle));
  gl->bindBuffer(GL_ARRAY_BUFFER, 0);
  gl->drawArrays(GL_TRIANGLE_STRIP, 0, 4);
  if (filterProgram->vertexArray > 0) {
    gl->bindVertexArray(0);
  }
  return CheckGLError(gl);
}

static Point ToGLVertexPoint(const Point& point, const BackendRenderTarget& target) {
  return {2.0f * point.x / static_cast<float>(target.width()) - 1.0f,
          2.0f * point.y / static_cast<float>(target.height()) - 1.0f};
}

static Point ToGLTexturePoint(const Point& point, const BackendTexture& source) {
  return {point.x / static_cast<float>(source.width()),
          point.y / static_cast<float>(source.height())};
}

std::vector<float> CornerPinEffect::computeVertices(const BackendTexture& source,
                                                    const BackendRenderTarget& target,
                                                    const Point& offset) const {
  std::vector<float> vertices = {};
  auto textureWidth = static_cast<float>(source.width());
  auto textureHeight = static_cast<float>(source.height());
  Point texturePoints[4] = {{0.0f, textureHeight},
                            {textureWidth, textureHeight},
                            {0.0f, 0.0f},
                            {textureWidth, 0.0f}};
  for (size_t i = 0; i < 4; i++) {
    auto vertexPoint = ToGLVertexPoint(cornerPoints[i] + offset, target);
    vertices.push_back(vertexPoint.x);
    vertices.push_back(vertexPoint.y);
    auto texturePoint = ToGLTexturePoint(texturePoints[i], source);
    vertices.push_back(texturePoint.x * vertexQs[i]);
    vertices.push_back(texturePoint.y * vertexQs[i]);
    vertices.push_back(vertexQs[i]);
  }
  return vertices;
}

static float calculateDistance(const Point& intersection, const Point& vertexPoint) {
  return std::sqrtf(std::powf(fabsf(intersection.x - vertexPoint.x), 2) +
                    std::powf(fabsf(intersection.y - vertexPoint.y), 2));
}

static bool PointIsBetween(const tgfx::Point& point, const tgfx::Point& start,
                           const tgfx::Point& end) {
  auto minX = std::min(start.x, end.x);
  auto maxX = std::max(start.x, end.x);
  auto minY = std::min(start.y, end.y);
  auto maxY = std::max(start.y, end.y);
  return minX <= point.x && point.x <= maxX && minY <= point.y && point.y <= maxY;
}

void CornerPinEffect::calculateVertexQs() {
  // https://www.reedbeta.com/blog/quadrilateral-interpolation-part-1/
  // compute the intersection of the two diagonals: y1 = k1 * x1 + b1; y2 = k2 * x2 + b2
  auto& lowerLeft = cornerPoints[0];
  auto& lowerRight = cornerPoints[1];
  auto& upperLeft = cornerPoints[2];
  auto& upperRight = cornerPoints[3];
  auto ll2ur_k = (upperRight.y - lowerLeft.y) / (upperRight.x - lowerLeft.x);
  auto ul2lr_k = (lowerRight.y - upperLeft.y) / (lowerRight.x - upperLeft.x);
  auto ll2ur_b = lowerLeft.y - ll2ur_k * lowerLeft.x;
  auto ul2lr_b = upperLeft.y - ul2lr_k * upperLeft.x;
  tgfx::Point intersection = {0, 0};
  intersection.x = (ul2lr_b - ll2ur_b) / (ll2ur_k - ul2lr_k);
  intersection.y = ll2ur_k * intersection.x + ll2ur_b;
  // compute the distance between the intersection and the 4 vertices
  auto lowerLeftDistance = calculateDistance(intersection, lowerLeft);
  auto lowerRightDistance = calculateDistance(intersection, lowerRight);
  auto upperRightDistance = calculateDistance(intersection, upperRight);
  auto upperLeftDistance = calculateDistance(intersection, upperLeft);
  // compute the uvq of the 4 vertices : uvq0 = float3(u0, v0, 1) * (d0 + d2) / d2
  if (PointIsBetween(intersection, lowerLeft, upperRight) &&
      PointIsBetween(intersection, upperLeft, lowerRight) && upperRightDistance != 0 &&
      upperLeftDistance != 0 && lowerRightDistance != 0 && lowerLeftDistance != 0) {
    vertexQs[0] = (lowerLeftDistance + upperRightDistance) / upperRightDistance;  // LowerLeft
    vertexQs[1] = (lowerRightDistance + upperLeftDistance) / upperLeftDistance;   // LowerRight
    vertexQs[2] = (upperLeftDistance + lowerRightDistance) / lowerRightDistance;  // UpperLeft
    vertexQs[3] = (upperRightDistance + lowerLeftDistance) / lowerLeftDistance;   // UpperRight
  } else {
    vertexQs[0] = 1.0f;
    vertexQs[1] = 1.0f;
    vertexQs[2] = 1.0f;
    vertexQs[3] = 1.0f;
  }
}
}  // namespace tgfx
