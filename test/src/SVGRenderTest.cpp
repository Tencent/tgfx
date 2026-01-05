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

#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/utils/Types.h"
#include "gtest/gtest.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/SVGCustomParser.h"
#include "tgfx/svg/SVGCustomWriter.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGExporter.h"
#include "tgfx/svg/node/SVGNode.h"
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

TGFX_TEST(SVGRenderTest, XMLParseLarge) {
  // This test verifies that a large SVG with many nodes can be parsed
  // and that the DOM can be properly destructed without issues.
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/4w_node.svg"));
  EXPECT_TRUE(stream != nullptr);
  auto xmlDOM = DOM::Make(*stream);
  EXPECT_TRUE(xmlDOM != nullptr);
}

TGFX_TEST(SVGRenderTest, PathSVG) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/path.svg"));
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/textEmoji"));
}

TGFX_TEST(SVGRenderTest, ComplexSVG) {

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  {
    auto stream = Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/complex1.svg"));
    ASSERT_TRUE(stream != nullptr);

    auto SVGDom = SVGDOM::Make(*stream);
    auto rootNode = SVGDom->getRoot();
    ASSERT_TRUE(rootNode != nullptr);

    auto surface = Surface::Make(context, 100, 100);
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
    auto canvas = surface->getCanvas();

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
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto size = SVGDom->getContainerSize();
  auto surface =
      Surface::Make(context, static_cast<int>(size.width), static_cast<int>(size.height));
  auto canvas = surface->getCanvas();

  SVGDom->render(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "SVGTest/referenceStyle"));
}

TGFX_TEST(SVGRenderTest, SaveCustomAttribute) {
  std::string SVGString = R"(
    <svg width="100" height="100" copyright="Tencent" producer="TGFX">
      <rect width="100%" height="100%" fill="red" producer="TGFX"/>
    </svg>
  )";

  auto data = Data::MakeWithCopy(SVGString.c_str(), SVGString.size());
  auto stream = Stream::MakeFromData(data);
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  {
    auto attribute = rootNode->getCustomAttributes();
    EXPECT_TRUE(attribute.size() == 2);
    EXPECT_TRUE(attribute[0].name == "copyright");
    EXPECT_TRUE(attribute[0].value == "Tencent");
    EXPECT_TRUE(attribute[1].name == "producer");
    EXPECT_TRUE(attribute[1].value == "TGFX");
  }

  auto children = rootNode->getChildren();
  EXPECT_TRUE(children.size() == 1);
  auto rectNode = children[0];
  {
    auto attribute = rectNode->getCustomAttributes();
    EXPECT_TRUE(attribute.size() == 1);
    EXPECT_TRUE(attribute[0].name == "producer");
    EXPECT_TRUE(attribute[0].value == "TGFX");
  }
}

TGFX_TEST(SVGRenderTest, IDAttribute) {
  std::string SVGString = R"(
      <svg width="100" height="100">
        <filter id="blur1">
          <feGaussianBlur stdDeviation="3"/>
        </filter>
      </svg>
    )";
  auto data = Data::MakeWithCopy(SVGString.c_str(), SVGString.size());
  auto stream = Stream::MakeFromData(data);
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream);
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);

  auto id = rootNode->getID();
  EXPECT_FALSE(id.isValue());

  auto children = rootNode->getChildren();
  EXPECT_TRUE(children.size() == 1);
  auto filterNode = children[0];
  auto filterID = filterNode->getID();
  EXPECT_TRUE(filterID.isValue());
  EXPECT_TRUE(filterID.get().value() == "blur1");
}

TGFX_TEST(SVGRenderTest, FilterCustomAttribute) {
  class FilterAttributeSetter : public SVGCustomParser {
    void handleCustomAttribute(SVGNode& node, const std::string& name,
                               const std::string& value) override {
      if ((node.tag() == SVGTag::Svg && name == "copyright") ||
          (node.tag() != SVGTag::Svg && name == "producer")) {
        node.addCustomAttribute(name, value);
      }
    }
  };

  std::string SVGString = R"(
    <svg width="100" height="100" copyright="Tencent" producer="TGFX">
      <rect width="100%" height="100%" fill="red" copyright="Tencent" producer="TGFX"/>
    </svg>
  )";

  auto data = Data::MakeWithCopy(SVGString.c_str(), SVGString.size());
  auto stream = Stream::MakeFromData(data);
  ASSERT_TRUE(stream != nullptr);
  auto SVGDom = SVGDOM::Make(*stream, nullptr, std::make_shared<FilterAttributeSetter>());
  auto rootNode = SVGDom->getRoot();
  ASSERT_TRUE(rootNode != nullptr);
  {
    auto attribute = rootNode->getCustomAttributes();
    EXPECT_TRUE(attribute.size() == 1);
    EXPECT_TRUE(attribute[0].name == "copyright");
    EXPECT_TRUE(attribute[0].value == "Tencent");
  }

  auto children = rootNode->getChildren();
  EXPECT_TRUE(children.size() == 1);
  auto rectNode = children[0];
  {
    auto attribute = rectNode->getCustomAttributes();
    EXPECT_TRUE(attribute.size() == 1);
    EXPECT_TRUE(attribute[0].name == "producer");
    EXPECT_TRUE(attribute[0].value == "TGFX");
  }
}

// Custom protocol: use "filter-data" attribute to store Filter parameters
// Blur: "blur:blurX,blurY,tileMode"
// DropShadow: "dropshadow:dx,dy,blurX,blurY,r,g,b,a,shadowOnly"
// InnerShadow: "innershadow:dx,dy,blurX,blurY,r,g,b,a,shadowOnly"
class ProtocolWriter : public SVGCustomWriter {
 public:
  DOMAttribute writeBlurImageFilter(float blurrinessX, float blurrinessY,
                                    TileMode tileMode) override {
    std::string value = "blur:" + std::to_string(blurrinessX) + "," + std::to_string(blurrinessY) +
                        "," + std::to_string(static_cast<int>(tileMode));
    return {"filter-data", value};
  }

  DOMAttribute writeDropShadowImageFilter(float dx, float dy, float blurrinessX, float blurrinessY,
                                          Color color, bool dropShadowOnly) override {
    std::string value = "dropshadow:" + std::to_string(dx) + "," + std::to_string(dy) + "," +
                        std::to_string(blurrinessX) + "," + std::to_string(blurrinessY) + "," +
                        std::to_string(color.red) + "," + std::to_string(color.green) + "," +
                        std::to_string(color.blue) + "," + std::to_string(color.alpha) + "," +
                        (dropShadowOnly ? "1" : "0");
    return {"filter-data", value};
  }

  DOMAttribute writeInnerShadowImageFilter(float dx, float dy, float blurrinessX, float blurrinessY,
                                           Color color, bool innerShadowOnly) override {
    std::string value = "innershadow:" + std::to_string(dx) + "," + std::to_string(dy) + "," +
                        std::to_string(blurrinessX) + "," + std::to_string(blurrinessY) + "," +
                        std::to_string(color.red) + "," + std::to_string(color.green) + "," +
                        std::to_string(color.blue) + "," + std::to_string(color.alpha) + "," +
                        (innerShadowOnly ? "1" : "0");
    return {"filter-data", value};
  }
};

class ProtocolSetter : public SVGCustomParser {
 public:
  void handleCustomAttribute(SVGNode& node, const std::string& name,
                             const std::string& value) override {
    if (node.tag() != SVGTag::Filter || name != "filter-data") {
      return;
    }
    auto id = node.getID();
    if (!id.isValue()) {
      return;
    }
    auto idString = id.get().value();

    // Parse filter-data attribute
    size_t colonPos = value.find(':');
    if (colonPos == std::string::npos) {
      return;
    }

    auto filterType = value.substr(0, colonPos);
    std::string paramsStr = value.substr(colonPos + 1);

    // Parse parameters
    std::vector<float> params;
    size_t start = 0;
    size_t commaPos;
    while ((commaPos = paramsStr.find(',', start)) != std::string::npos) {
      params.push_back(std::stof(paramsStr.substr(start, commaPos - start)));
      start = commaPos + 1;
    }
    params.push_back(std::stof(paramsStr.substr(start)));

    // Build ImageFilter based on filter type
    std::shared_ptr<ImageFilter> imageFilter;
    if (filterType == "blur") {
      // blur:blurX,blurY,tileMode
      if (params.size() >= 3) {
        float blurX = params[0];
        float blurY = params[1];
        auto tileMode = static_cast<TileMode>(static_cast<int>(params[2]));
        imageFilter = ImageFilter::Blur(blurX, blurY, tileMode);
      }
    } else if (filterType == "dropshadow") {
      // dropshadow:dx,dy,blurX,blurY,r,g,b,a,shadowOnly
      if (params.size() >= 9) {
        float dx = params[0];
        float dy = params[1];
        float blurX = params[2];
        float blurY = params[3];
        Color color{params[4], params[5], params[6], params[7]};
        bool shadowOnly = params[8] > 0.5f;
        if (shadowOnly) {
          imageFilter = ImageFilter::DropShadowOnly(dx, dy, blurX, blurY, color);
        } else {
          imageFilter = ImageFilter::DropShadow(dx, dy, blurX, blurY, color);
        }
      }
    } else if (filterType == "innershadow") {
      // innershadow:dx,dy,blurX,blurY,r,g,b,a,shadowOnly
      if (params.size() >= 9) {
        float dx = params[0];
        float dy = params[1];
        float blurX = params[2];
        float blurY = params[3];
        Color color{params[4], params[5], params[6], params[7]};
        bool shadowOnly = params[8] > 0.5f;
        if (shadowOnly) {
          imageFilter = ImageFilter::InnerShadowOnly(dx, dy, blurX, blurY, color);
        } else {
          imageFilter = ImageFilter::InnerShadow(dx, dy, blurX, blurY, color);
        }
      }
    }
    // Store Filter to map with id as key
    if (imageFilter) {
      filterMap[idString] = imageFilter;
    }
  }

  std::unordered_map<std::string, std::shared_ptr<ImageFilter>> filterMap;
};

TGFX_TEST(SVGRenderTest, ProtocolFilterReadWrite) {
  // Test Writer functionality
  auto protocolWriter = std::make_shared<ProtocolWriter>();

  ContextScope scope;
  auto context = scope.getContext();
  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(300, 300), 0, protocolWriter);
  auto canvas = exporter->getCanvas();

  auto blurFilter = ImageFilter::Blur(10.f, 10.f, TileMode::Clamp);
  auto dropShadowFilter =
      ImageFilter::DropShadow(10.f, 10.f, 20.f, 20.f, Color{0.0f, 0.0f, 0.0f, 0.5f});
  auto innerShadowFilter =
      ImageFilter::InnerShadow(10.f, 10.f, 20.f, 20.f, Color{0.0f, 0.0f, 0.0f, 0.5f});

  Paint paint;
  paint.setImageFilter(blurFilter);
  canvas->drawRect(Rect::MakeXYWH(0.f, 0.f, 100.f, 100.f), paint);
  paint.setImageFilter(dropShadowFilter);
  canvas->drawRect(Rect::MakeXYWH(100.f, 100.f, 100.f, 100.f), paint);
  paint.setImageFilter(innerShadowFilter);
  canvas->drawRect(Rect::MakeXYWH(200.f, 200.f, 100.f, 100.f), paint);

  exporter->close();

  // Test Setter parsing functionality
  auto stream = Stream::MakeFromData(SVGStream->readData());
  auto protocolSetter = std::make_shared<ProtocolSetter>();
  auto SVGDom = SVGDOM::Make(*stream, nullptr, protocolSetter);

  // Verify parsed Filters
  EXPECT_TRUE(protocolSetter->filterMap.size() >= 3);  // At least 3 filters

  for (const auto& [_, filter] : protocolSetter->filterMap) {
    if (filter) {
      auto type = Types::Get(filter.get());
      if (type == Types::ImageFilterType::Blur) {
        auto blur1 = std::static_pointer_cast<GaussianBlurImageFilter>(filter);
        auto blur2 = std::static_pointer_cast<GaussianBlurImageFilter>(blurFilter);
        EXPECT_EQ(blur1->blurrinessX, blur2->blurrinessX);
        EXPECT_EQ(blur1->blurrinessY, blur2->blurrinessY);
        EXPECT_EQ(blur1->tileMode, blur2->tileMode);
      } else if (type == Types::ImageFilterType::DropShadow) {
        auto dropShadow1 = std::static_pointer_cast<DropShadowImageFilter>(filter);
        auto dropShadow2 = std::static_pointer_cast<DropShadowImageFilter>(dropShadowFilter);
        EXPECT_EQ(dropShadow1->dx, dropShadow2->dx);
        EXPECT_EQ(dropShadow1->dy, dropShadow2->dy);
        EXPECT_EQ(dropShadow1->color, dropShadow2->color);
        EXPECT_EQ(dropShadow1->shadowOnly, dropShadow2->shadowOnly);
        auto blur1 = std::static_pointer_cast<GaussianBlurImageFilter>(dropShadow1->blurFilter);
        auto blur2 = std::static_pointer_cast<GaussianBlurImageFilter>(dropShadow2->blurFilter);
        EXPECT_EQ(blur1->blurrinessX, blur2->blurrinessX);
        EXPECT_EQ(blur1->blurrinessY, blur2->blurrinessY);
        EXPECT_EQ(blur1->tileMode, blur2->tileMode);
      } else if (type == Types::ImageFilterType::InnerShadow) {
        auto innerShadow1 = std::static_pointer_cast<InnerShadowImageFilter>(filter);
        auto innerShadow2 = std::static_pointer_cast<InnerShadowImageFilter>(innerShadowFilter);
        EXPECT_EQ(innerShadow1->dx, innerShadow2->dx);
        EXPECT_EQ(innerShadow1->dy, innerShadow2->dy);
        EXPECT_EQ(innerShadow1->color, innerShadow2->color);
        EXPECT_EQ(innerShadow1->shadowOnly, innerShadow2->shadowOnly);
        auto blur1 = std::static_pointer_cast<GaussianBlurImageFilter>(innerShadow1->blurFilter);
        auto blur2 = std::static_pointer_cast<GaussianBlurImageFilter>(innerShadow2->blurFilter);
        EXPECT_EQ(blur1->blurrinessX, blur2->blurrinessX);
        EXPECT_EQ(blur1->blurrinessY, blur2->blurrinessY);
        EXPECT_EQ(blur1->tileMode, blur2->tileMode);
      }
    }
  }
}

}  // namespace tgfx
