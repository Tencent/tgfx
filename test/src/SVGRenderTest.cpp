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

#include "gtest/gtest.h"
#include "tgfx/core/Data.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/xml/XMLDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(SVGRenderTest, XMLParse) {

  std::string xml = R"(
    <svg width="100" height="100">
      <rect width="100%" height="100%" fill="red" />
      <circle cx="150" cy="100" r="80" fill="green" />
    </svg>
  )";

  auto data = Data::MakeWithCopy(xml.data(), xml.size());
  EXPECT_TRUE(data != nullptr);
  auto xmlDOM = DOM::MakeFromData(*data);
  EXPECT_TRUE(xmlDOM != nullptr);

  auto rootNode = xmlDOM->getRootNode();
  EXPECT_TRUE(rootNode != nullptr);
  EXPECT_EQ(rootNode->name, "svg");
  EXPECT_EQ(static_cast<int>(rootNode->attributes.size()), 2);
  EXPECT_EQ(rootNode->attributes[0].name, "width");
  EXPECT_EQ(rootNode->attributes[0].value, "100");
  EXPECT_EQ(rootNode->attributes[1].name, "height");
  EXPECT_EQ(rootNode->attributes[1].value, "100");

  EXPECT_EQ(rootNode->countChildren(), 2);
  auto rectNode = rootNode->getFirstChild("");
  EXPECT_TRUE(rectNode != nullptr);
  EXPECT_EQ(rectNode->name, "rect");
  EXPECT_EQ(static_cast<int>(rectNode->attributes.size()), 3);
  EXPECT_EQ(rectNode->attributes[0].name, "width");
  EXPECT_EQ(rectNode->attributes[0].value, "100%");
  EXPECT_EQ(rectNode->attributes[1].name, "height");
  EXPECT_EQ(rectNode->attributes[1].value, "100%");
  EXPECT_EQ(rectNode->attributes[2].name, "fill");
  EXPECT_EQ(rectNode->attributes[2].value, "red");

  auto circleNode = rectNode->getNextSibling("");
  EXPECT_TRUE(circleNode != nullptr);
  bool isGood = false;
  std::string value;
  std::tie(isGood, value) = circleNode->findAttribute("cx");
  EXPECT_TRUE(isGood);
  EXPECT_EQ(value, "150");
  std::tie(isGood, value) = circleNode->findAttribute("cy");
  EXPECT_TRUE(isGood);
  EXPECT_EQ(value, "100");
  std::tie(isGood, value) = circleNode->findAttribute("round");
  EXPECT_FALSE(isGood);

  auto copyDOM = DOM::copy(xmlDOM);
  EXPECT_TRUE(copyDOM != nullptr);
  EXPECT_TRUE(copyDOM.get() != xmlDOM.get());
  EXPECT_EQ(copyDOM->getRootNode()->name, xmlDOM->getRootNode()->name);
}

TGFX_TEST(SVGRenderTest, PathSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/path.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/path"));
}

TGFX_TEST(SVGRenderTest, PNGImageSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/png.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/png_image"));
}

TGFX_TEST(SVGRenderTest, JPGImageSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/jpg.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/jpg_image"));
}

TGFX_TEST(SVGRenderTest, MaskSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/mask.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/mask"));
}

TGFX_TEST(SVGRenderTest, GradientSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/radialGradient.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/radialGradient"));
}

TGFX_TEST(SVGRenderTest, BlurSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/blur.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/blur"));
}

TGFX_TEST(SVGRenderTest, TextSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/text.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  auto fontManager = SVGFontManager::Make(typeface);
  ASSERT_TRUE(fontManager != nullptr);

  SVGDom->render(canvas, fontManager);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/text"));
}

TGFX_TEST(SVGRenderTest, TextFontSVG) {
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/textFont.svg"));
  ASSERT_TRUE(data != nullptr);
  auto SVGDom = SVGDOM::Make(data);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  auto defaultTypeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(defaultTypeface != nullptr);
  auto fontManager = SVGFontManager::Make(defaultTypeface);
  ASSERT_TRUE(fontManager != nullptr);

  SVGDom->collectRenderFonts(fontManager);
  auto families = fontManager->getFontFamilies();
  ASSERT_TRUE(families.size() == 1);
  auto family = fontManager->getFontFamilies()[0];
  ASSERT_TRUE(family == "Noto Serif SC");
  auto infos = fontManager->getFontInfos(family);
  ASSERT_TRUE(infos.size() == 1);
  auto info = infos[0];
  ASSERT_TRUE(info.weight() == SVGFontWeight::Type::Normal);
  ASSERT_TRUE(info.style() == SVGFontStyle::Type::Normal);

  auto typeface = MakeTypeface("resources/font/NotoSerifSC-Regular.otf");
  fontManager->setTypeface(family, info, typeface);

  SVGDom->render(canvas, fontManager);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/textFont"));
}

}  // namespace tgfx