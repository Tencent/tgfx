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

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include "gtest/gtest.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGRadialGradient.h"
#include "tgfx/svg/node/SVGRect.h"
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
  auto stream = Stream::MakeFromData(data);
  EXPECT_TRUE(stream != nullptr);
  auto xmlDOM = DOM::Make(*stream);
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

TGFX_TEST(SVGRenderTest, SVGParse) {
  /*
  <svg width="100" height="100" viewBox="0 0 100 100" fill="none" xmlns="http://www.w3.org/2000/svg">
    <rect width="100" height="100" fill="url(#paint0_radial_88_7)"/>
    <defs>
      <radialGradient id="paint0_radial_88_7" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(50 50) rotate(90) scale(50)">
        <stop stop-color="#0800FF"/>
        <stop offset="0.49" stop-color="#D2E350"/>
        <stop offset="1" stop-color="#74D477"/>
      </radialGradient>
    </defs>
  </svg>
  */

  auto stream =
      Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/radialGradient.svg"));
  EXPECT_TRUE(stream != nullptr);
  auto svgDOM = SVGDOM::Make(*stream);
  auto root = svgDOM->getRoot();
  auto nodeIDMap = svgDOM->nodeIDMapper();

  auto children = root->getChildren();
  // EXPECT_EQ(children.size(), 1U);
  auto childNode0 = children[0];
  if (childNode0->tag() == SVGTag::Rect) {
    auto rectNode = std::static_pointer_cast<SVGRect>(childNode0);
    rectNode->getWidth();
    rectNode->getHeight();
    // SVGProperty<SVGPaint, true> svgPaint =rectNode->getFill();
    auto svgPaint = rectNode->getFill();
    if (svgPaint.isValue()) {
      if (svgPaint->type() == SVGPaint::Type::Color) {
        //normal color
        auto color = svgPaint->color();
      } else if (svgPaint->type() == SVGPaint::Type::IRI) {
        auto iri = svgPaint->iri();
        const auto& nodeID = iri.iri();
        auto iter = nodeIDMap.find(nodeID);
        if (iter != nodeIDMap.end() && iter->second) {
          if (iter->second->tag() == SVGTag::RadialGradient) {
            auto radialGradient = std::static_pointer_cast<SVGRadialGradient>(iter->second);
            radialGradient->getGradientTransform();
            radialGradient->getGradientUnits();
            auto center =
                Point::Make(radialGradient->getCx().value(), radialGradient->getCy().value());
            auto radius = radialGradient->getR();

            std::vector<Color> colors;
            std::vector<float> offsets;
            for (const auto& node : radialGradient->getChildren()) {
              auto stop = std::static_pointer_cast<SVGStop>(node);
              auto offset = stop->getOffset();
              offsets.push_back(offset.value());
              auto stopColor = stop->getStopColor();
              colors.push_back(stopColor->color());
            }
            Shader::MakeRadialGradient(center, radius.value(), colors, offsets);
          }
        }
      }
    }
  }
}

TGFX_TEST(SVGRenderTest, PathSVG) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/path.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/png.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/jpg.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/mask.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream =
      Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/radialGradient.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/blur.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
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
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/text.svg"));
  ASSERT_TRUE(stream != nullptr);

  std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList;
  fallbackTypefaceList.push_back(
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf")));

  auto SVGDom = SVGDOM::Make(*stream, TextShaper::Make(fallbackTypefaceList));
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/text"));
}

TGFX_TEST(SVGRenderTest, TextFontSVG) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/textFont.svg"));
  ASSERT_TRUE(stream != nullptr);

  std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList;
  fallbackTypefaceList.push_back(
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf")));

  auto SVGDom = SVGDOM::Make(*stream, TextShaper::Make(fallbackTypefaceList));
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/textFont"));
}

TGFX_TEST(SVGRenderTest, TextEmojiSVG) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/emoji.svg"));
  ASSERT_TRUE(stream != nullptr);

  std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList;
  fallbackTypefaceList.push_back(
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf")));
  fallbackTypefaceList.push_back(
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf")));

  auto SVGDom = SVGDOM::Make(*stream, TextShaper::Make(fallbackTypefaceList));
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/textEmoji"));
}

TGFX_TEST(SVGRenderTest, ComplexSVG) {

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex1.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 100, 100);
    auto* canvas = surface->getCanvas();

    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex1"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex2.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 160, 160);
    auto* canvas = surface->getCanvas();

    canvas->scale(10, 10);
    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex2"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex3.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 300, 300);
    auto* canvas = surface->getCanvas();

    canvas->scale(2, 2);
    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex3"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex4.svg"));
    ASSERT_TRUE(stream != nullptr);

    std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList;
    fallbackTypefaceList.push_back(
        Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf")));

    auto SVGDom = SVGDOM::Make(*stream, TextShaper::Make(fallbackTypefaceList));
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 500, 400);
    auto* canvas = surface->getCanvas();

    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex4"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex5.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 1300, 1300);
    auto* canvas = surface->getCanvas();

    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex5"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex6.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 375, 812);
    auto* canvas = surface->getCanvas();

    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex6"));
  }

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex7.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 1090, 2026);
    auto* canvas = surface->getCanvas();

    SVGDom->render(canvas);
    EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/complex7"));
  }
}

TGFX_TEST(SVGRenderTest, ReferenceStyleSVG) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/refStyle.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, static_cast<int>(rootNode->getWidth().value()),
                               static_cast<int>(rootNode->getHeight().value()));
  auto* canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/referenceStyle"));
}

}  // namespace tgfx