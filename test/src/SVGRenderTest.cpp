/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/SVGDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(SVGRenderTest, PathSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/path.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/path"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, PNGImageSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/png.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/png_image"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, JPGImageSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/jpg.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/jpg_image"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, MaskSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/mask.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/mask"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, GradientSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/radialGradient.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/radialGradient"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, BlurSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/blur.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/blur"));
  device->unlock();
}

TGFX_TEST(SVGRenderTest, TextSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/text.svg"));
  ASSERT_TRUE(data != nullptr);
  auto fontManager = std::make_shared<tgfx::SVGFontManager>();
  ASSERT_TRUE(fontManager != nullptr);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  fontManager->setDefaultTypeface(typeface);

  auto SVGDom = SVGDOM::Builder().make(*data, fontManager);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  ASSERT_TRUE(device != nullptr);
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/text"));
  device->unlock();
}

}  // namespace tgfx