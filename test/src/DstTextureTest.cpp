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
#include "core/Records.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/AppendShape.h"
#include "core/shapes/ProviderShape.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

namespace tgfx {
TGFX_TEST(DstTextureTest, EmptyLocalBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 800;
  auto height = 600;
  auto renderTarget = RenderTarget::Make(context, width, height);
  auto backendRenderTarget = renderTarget->getBackendRenderTarget();

  auto surface = Surface::MakeFrom(context, backendRenderTarget, ImageOrigin::BottomLeft, 0,
                                   renderTarget->colorSpace());
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
  EXPECT_TRUE(Baseline::Compare(surface, "DstTextureTest/EmptyLocalBounds"));
}

TGFX_TEST(DstTextureTest, OutOfRenderTarget) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 800;
  auto height = 600;
  auto renderTarget = RenderTarget::Make(context, width, height);
  auto backendRenderTarget = renderTarget->getBackendRenderTarget();

  auto surface = Surface::MakeFrom(context, backendRenderTarget, ImageOrigin::BottomLeft, 0,
                                   renderTarget->colorSpace());
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
  EXPECT_TRUE(Baseline::Compare(surface, "DstTextureTest/OutOfRenderTarget"));
}

}  // namespace tgfx
