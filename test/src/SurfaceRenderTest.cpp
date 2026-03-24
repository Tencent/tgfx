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
#include "gpu/resources/RenderTarget.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
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

// ==================== SampleCount Propagation Tests ====================

TGFX_TEST(SurfaceRenderTest, SampleCountAPI) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Surface::Make with default sampleCount (1).
  auto surface1x = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface1x != nullptr);
  EXPECT_EQ(surface1x->sampleCount(), 1);

  // Surface::Make with sampleCount=4.
  auto surface4x = Surface::Make(context, 100, 100, false, 4);
  ASSERT_TRUE(surface4x != nullptr);
  EXPECT_TRUE(surface4x->sampleCount() > 1);

  // Surface::MakeFrom(BackendTexture) with sampleCount=4.
  auto texture = context->gpu()->createTexture({100, 100, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  auto surfaceFromTexture =
      Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::TopLeft, 4);
  ASSERT_TRUE(surfaceFromTexture != nullptr);
  EXPECT_TRUE(surfaceFromTexture->sampleCount() > 1);

  // Surface::MakeFrom(BackendRenderTarget) carries sampleCount from BackendRenderTarget itself.
  auto rt = RenderTarget::Make(context, 100, 100, PixelFormat::RGBA_8888, 4);
  ASSERT_TRUE(rt != nullptr);
  auto backendRT = rt->getBackendRenderTarget();
  EXPECT_TRUE(backendRT.sampleCount() > 1);
  auto surfaceFromRT = Surface::MakeFrom(context, backendRT, ImageOrigin::TopLeft);
  ASSERT_TRUE(surfaceFromRT != nullptr);
  EXPECT_TRUE(surfaceFromRT->sampleCount() > 1);
}

TGFX_TEST(SurfaceRenderTest, SampleCountRendering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Build a five-pointed star path centered at (200, 200) with outer radius 160 and inner radius 70.
  // The star must be large enough to trigger GPU triangle tessellation instead of CPU rasterization.
  Path star;
  auto outerRadius = 160.f;
  auto innerRadius = 70.f;
  auto centerX = 200.f;
  auto centerY = 200.f;
  for (int i = 0; i < 5; i++) {
    auto outerAngle = static_cast<float>(-M_PI_2 + i * 2 * M_PI / 5);
    auto innerAngle = static_cast<float>(-M_PI_2 + (i + 0.5f) * 2 * M_PI / 5);
    auto outerX = centerX + outerRadius * cosf(outerAngle);
    auto outerY = centerY + outerRadius * sinf(outerAngle);
    auto innerX = centerX + innerRadius * cosf(innerAngle);
    auto innerY = centerY + innerRadius * sinf(innerAngle);
    if (i == 0) {
      star.moveTo(outerX, outerY);
    } else {
      star.lineTo(outerX, outerY);
    }
    star.lineTo(innerX, innerY);
  }
  star.close();

  // Disable antiAlias so only MSAA can provide anti-aliasing.
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color::Red());

  // Draw the star on a 1x surface — edges should have hard aliased pixels.
  auto surface1x = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface1x != nullptr);
  auto canvas = surface1x->getCanvas();
  canvas->clear(Color::White());
  canvas->drawPath(star, paint);
  EXPECT_TRUE(Baseline::Compare(surface1x, "SurfaceRenderTest/SampleCountRendering_1x"));

  // Draw the same star on a 4x MSAA surface — edges should be smoother due to MSAA.
  auto surface4x = Surface::Make(context, 400, 400, false, 4);
  ASSERT_TRUE(surface4x != nullptr);
  canvas = surface4x->getCanvas();
  canvas->clear(Color::White());
  canvas->drawPath(star, paint);
  EXPECT_TRUE(Baseline::Compare(surface4x, "SurfaceRenderTest/SampleCountRendering_4x"));
}

TGFX_TEST(SurfaceRenderTest, SampleCountWithClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Build a rounded rectangle path for clipping.
  Path clipRoundRect;
  clipRoundRect.addRoundRect(Rect::MakeLTRB(10, 10, 190, 190), 30, 30);

  // Disable antiAlias so only MSAA can provide anti-aliasing on clip edges.
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color::Red());

  // Draw a filled rect clipped by a round rect on a 1x surface.
  auto surface1x = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface1x != nullptr);
  auto canvas = surface1x->getCanvas();
  canvas->clear(Color::White());
  canvas->clipPath(clipRoundRect);
  canvas->drawRect(Rect::MakeWH(200, 200), paint);
  EXPECT_TRUE(Baseline::Compare(surface1x, "SurfaceRenderTest/SampleCountWithClip_1x"));

  // Same drawing on a 4x MSAA surface — clip edge should be smoother.
  auto surface4x = Surface::Make(context, 200, 200, false, 4);
  ASSERT_TRUE(surface4x != nullptr);
  canvas = surface4x->getCanvas();
  canvas->clear(Color::White());
  canvas->clipPath(clipRoundRect);
  canvas->drawRect(Rect::MakeWH(200, 200), paint);
  EXPECT_TRUE(Baseline::Compare(surface4x, "SurfaceRenderTest/SampleCountWithClip_4x"));
}

TGFX_TEST(SurfaceRenderTest, SampleCountSnapshotRoundTrip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Create a 4x MSAA surface and draw a five-pointed star on it.
  auto surface = Surface::Make(context, 400, 400, false, 4);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(surface->sampleCount() > 1);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Build a five-pointed star path centered at (200, 200).
  Path star;
  auto outerRadius = 140.f;
  auto innerRadius = 60.f;
  auto centerX = 200.f;
  auto centerY = 200.f;
  for (int i = 0; i < 5; i++) {
    auto outerAngle = static_cast<float>(-M_PI_2 + i * 2 * M_PI / 5);
    auto innerAngle = static_cast<float>(-M_PI_2 + (i + 0.5f) * 2 * M_PI / 5);
    auto outerX = centerX + outerRadius * cosf(outerAngle);
    auto outerY = centerY + outerRadius * sinf(outerAngle);
    auto innerX = centerX + innerRadius * cosf(innerAngle);
    auto innerY = centerY + innerRadius * sinf(innerAngle);
    if (i == 0) {
      star.moveTo(outerX, outerY);
    } else {
      star.lineTo(outerX, outerY);
    }
    star.lineTo(innerX, innerY);
  }
  star.close();

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color::Blue());
  canvas->drawPath(star, paint);

  // Snapshot the MSAA surface and redraw onto a 1x surface — the resolved content should be
  // preserved exactly.
  auto snapshot = surface->makeImageSnapshot();
  ASSERT_TRUE(snapshot != nullptr);
  auto compareSurface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(compareSurface != nullptr);
  EXPECT_EQ(compareSurface->sampleCount(), 1);
  auto compareCanvas = compareSurface->getCanvas();
  compareCanvas->drawImage(snapshot);
  EXPECT_TRUE(Baseline::Compare(compareSurface, "SurfaceRenderTest/SampleCountSnapshotRoundTrip"));
}

}  // namespace tgfx
