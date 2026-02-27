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

#include "base/LayerBuilders.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/DropShadowFilter.h"

namespace hello2d {

static inline tgfx::Matrix3D MakePerspectiveMatrix() {
  auto perspectiveMatrix = tgfx::Matrix3D::I();
  constexpr float eyeDistance = 1200.f;
  perspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);
  return perspectiveMatrix;
}

static inline std::shared_ptr<tgfx::Layer> CreateBackLayer(const tgfx::Point& origin) {
  auto layer = tgfx::ShapeLayer::Make();
  auto rect = tgfx::Rect::MakeWH(600, 400);
  tgfx::Path path = {};
  path.addRect(rect);
  layer->setPath(path);
  std::shared_ptr<tgfx::Shader> shader = tgfx::Shader::MakeLinearGradient(
      {rect.left, 0}, {rect.right, 0}, {tgfx::Color::Red(), tgfx::Color::Green()});
  layer->addFillStyle(tgfx::ShapeStyle::Make(shader, 1.f));
  auto matrix = tgfx::Matrix::MakeScale(0.5, 0.5);
  matrix.postTranslate(origin.x, origin.y);
  layer->setMatrix(matrix);
  return layer;
}

static inline std::shared_ptr<tgfx::Layer> CreateContainerLayer(const tgfx::Point& origin) {
  auto layer = tgfx::SolidLayer::Make();
  layer->setColor(tgfx::Color::FromRGBA(151, 153, 46, 255));
  auto layerSize = tgfx::Size::Make(360.f, 320.f);
  layer->setWidth(layerSize.width);
  layer->setHeight(layerSize.height);
  auto anchor = tgfx::Point::Make(0.3f, 0.3f);
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * layerSize.width, -anchor.y * layerSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * layerSize.width, anchor.y * layerSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeRotate({0.f, 1.f, 0.f}, -45.f);
  auto perspectiveMatrix = MakePerspectiveMatrix();
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                         modelMatrix * offsetToAnchorMatrix;
  layer->setMatrix3D(transformMatrix);
  return layer;
}

static inline float DegreesToRadians(float degrees) {
  return degrees * (static_cast<float>(M_PI) / 180.0f);
}

static inline std::shared_ptr<tgfx::Layer> CreateImageLayer(const AppHost* host,
                                                            const tgfx::Point& origin) {
  auto image = host->getImage("bridge");
  if (!image) {
    return nullptr;
  }

  auto shadowFilter = tgfx::DropShadowFilter::Make(-20, -20, 0, 0, tgfx::Color::Green());
  auto imageLayer = tgfx::ImageLayer::Make();
  imageLayer->setImage(image);
  imageLayer->setFilters({shadowFilter});
  auto imageSize =
      tgfx::Size::Make(static_cast<float>(image->width()), static_cast<float>(image->height()));
  auto anchor = tgfx::Point::Make(0.05f, 0.05f);
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeScale(2.f, 2.f, 1.f);
  constexpr float skewXDegrees = -15.f;
  constexpr float skewYDegrees = -15.f;
  modelMatrix.postSkewXY(tanf(DegreesToRadians(skewXDegrees)),
                         tanf(DegreesToRadians(skewYDegrees)));
  modelMatrix.postRotate({0.f, 0.f, 1.f}, 45.f);
  modelMatrix.preRotate({1.f, 0.f, 0.f}, 45.f);
  modelMatrix.preRotate({0.f, 1.f, 0.f}, 45.f);
  modelMatrix.postTranslate(0.f, 0.f, 20.f);
  modelMatrix.preScale(0.07f, 0.07f, 1.0f);
  auto perspectiveMatrix = MakePerspectiveMatrix();
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  auto imageMatrix3D = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                       modelMatrix * offsetToAnchorMatrix;
  imageLayer->setMatrix3D(imageMatrix3D);
  return imageLayer;
}

/**
 * Creates a background layer for the cube with a linear gradient fill and 3D transformation.
 * @param origin The origin point for positioning the layer.
 * @return A shared pointer to the created layer.
 */
static inline std::shared_ptr<tgfx::Layer> CreateCubeBackLayer(const tgfx::Point& origin) {
  auto layer = tgfx::ShapeLayer::Make();
  auto rect = tgfx::Rect::MakeWH(600, 400);
  tgfx::Path path = {};
  path.addRect(rect);
  layer->setPath(path);
  std::shared_ptr<tgfx::Shader> shader = tgfx::Shader::MakeLinearGradient(
      {rect.left, 0}, {rect.right, 0}, {tgfx::Color::Red(), tgfx::Color::Green()});
  layer->addFillStyle(tgfx::ShapeStyle::Make(shader, 1.f));

  auto layerSize = tgfx::Size::Make(600, 400);
  auto anchor = tgfx::Point::Make(0.5f, 0.5f);
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * layerSize.width, -anchor.y * layerSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * layerSize.width, anchor.y * layerSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeScale(0.5, 0.5, 1);
  modelMatrix.postRotate({0, 1, 0}, 10);
  auto perspectiveMatrix = MakePerspectiveMatrix();
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                         modelMatrix * offsetToAnchorMatrix;
  layer->setMatrix3D(transformMatrix);
  return layer;
}

/**
 * Creates a face layer for the cube with specified rotation and color.
 * @param anchor The anchor point for rotation (normalized coordinates).
 * @param rotateAxis The axis around which to rotate the face.
 * @param rotateAngles The rotation angle in degrees.
 * @param color The fill color of the face.
 * @return A shared pointer to the created solid layer.
 */
static inline std::shared_ptr<tgfx::SolidLayer> CreateCubeFaceLayer(const tgfx::Point& anchor,
                                                                    const tgfx::Vec3& rotateAxis,
                                                                    float rotateAngles,
                                                                    const tgfx::Color& color) {
  auto layer = tgfx::SolidLayer::Make();
  auto layerSize = tgfx::Size::Make(200, 200);
  layer->setWidth(layerSize.width);
  layer->setHeight(layerSize.height);
  layer->setColor(color);

  tgfx::Point origin = {200, 100};
  auto offsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(-anchor.x * layerSize.width, -anchor.y * layerSize.height, 0.f);
  auto invOffsetToAnchorMatrix =
      tgfx::Matrix3D::MakeTranslate(anchor.x * layerSize.width, anchor.y * layerSize.height, 0.f);
  auto modelMatrix = tgfx::Matrix3D::MakeRotate(rotateAxis, rotateAngles);
  auto originTranslateMatrix = tgfx::Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  auto transformMatrix =
      originTranslateMatrix * invOffsetToAnchorMatrix * modelMatrix * offsetToAnchorMatrix;
  layer->setMatrix3D(transformMatrix);
  return layer;
}

std::shared_ptr<tgfx::Layer> Layer3DTree::onBuildLayerTree(const AppHost* host) {
  auto root = tgfx::Layer::Make();

  // Flat 3D Container
  auto flat3DLayer = CreateImageLayer(host, {125, -50});
  auto flat3DContainerLayer = CreateContainerLayer({120, 40});
  flat3DContainerLayer->addChild(flat3DLayer);
  auto flat3DBackLayer = CreateBackLayer({0, 0});
  flat3DBackLayer->addChild(flat3DContainerLayer);
  root->addChild(flat3DBackLayer);

  // Preserve 3D Container
  auto preserve3DLayer = CreateImageLayer(host, {125, -50});
  auto preserve3DContainerLayer = CreateContainerLayer({120, 40});
  preserve3DContainerLayer->setPreserve3D(true);
  preserve3DContainerLayer->addChild(preserve3DLayer);
  auto preserve3DBackLayer = CreateBackLayer({0, 300});
  preserve3DBackLayer->addChild(preserve3DContainerLayer);
  root->addChild(preserve3DBackLayer);

  // Cube
  auto cubeBackLayer = CreateCubeBackLayer({200, 0});
  cubeBackLayer->setPreserve3D(true);
  root->addChild(cubeBackLayer);
  auto cubeFloor = CreateCubeFaceLayer({0.5, 0.5}, {1, 0, 0}, 0, tgfx::Color::White());
  cubeBackLayer->addChild(cubeFloor);
  auto cubeLeft = CreateCubeFaceLayer({0.0, 0.5}, {0, 1, 0}, -90, tgfx::Color::Red());
  cubeBackLayer->addChild(cubeLeft);
  auto cubeTop =
      CreateCubeFaceLayer({0.5, 0.0}, {1, 0, 0}, 90, tgfx::Color::FromRGBA(0, 153, 46, 255));
  cubeBackLayer->addChild(cubeTop);
  auto cubeRight =
      CreateCubeFaceLayer({1.0, 0.5}, {0, 1, 0}, 90, tgfx::Color::FromRGBA(90, 153, 146, 255));
  cubeBackLayer->addChild(cubeRight);
  auto cubeBottom =
      CreateCubeFaceLayer({0.5, 1.0}, {1, 0, 0}, -90, tgfx::Color::FromRGBA(190, 53, 146, 255));
  cubeBackLayer->addChild(cubeBottom);
  return root;
}
}  // namespace hello2d
