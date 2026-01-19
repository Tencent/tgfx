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
#include "gpu/opengl/GLUtil.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"
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

}  // namespace tgfx
