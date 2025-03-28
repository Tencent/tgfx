#include <iostream>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>
#include "gtest/gtest.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImagePattern.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGGroup.h"
#include "tgfx/svg/node/SVGImage.h"
#include "tgfx/svg/node/SVGLinearGradient.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPattern.h"
#include "tgfx/svg/node/SVGRadialGradient.h"
#include "tgfx/svg/node/SVGRect.h"
#include "tgfx/svg/node/SVGRoot.h"
#include "tgfx/svg/node/SVGShape.h"
#include "tgfx/svg/node/SVGUse.h"
#include "tgfx/svg/xml/XMLDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {

class SVGParser {
 public:
  SVGParser(const std::shared_ptr<SVGRoot>& root, const SVGIDMapper& nodeIDMapper)
      : root_(root), nodeIDMapper_(nodeIDMapper) {
    auto viewport = Size::MakeEmpty();
    if (root->getViewBox().has_value()) {
      viewport = root->getViewBox()->size();
    } else {
      viewport = Size::Make(root->getWidth().value(), root->getHeight().value());
    }
    SVGLengthContext lengthContext(viewport);

    auto rootLayer = Layer::Make();
    layerStack_.push(rootLayer);
    for (const auto& node : root->getChildren()) {
      parseNode(node, lengthContext);
    }
    layerStack_.pop();
  }

  void parsePaint(SVGPaint paint) {
    if (paint.type() == SVGPaint::Type::Color) {
      paint.color();
    } else if (paint.type() == SVGPaint::Type::IRI) {
      const auto& iri = paint.iri();
      const auto& nodeID = iri.iri();
      auto iter = nodeIDMapper_.find(nodeID);
      if (iter != nodeIDMapper_.end() && iter->second) {
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

  std::shared_ptr<ShapeStyle> parseRadialGradient(
      const std::shared_ptr<SVGRadialGradient>& radialGradient, SVGLengthContext lengthContext) {

    lengthContext.setBoundingBoxUnits(radialGradient->getGradientUnits());
    auto centerX =
        lengthContext.resolve(radialGradient->getCx(), SVGLengthContext::LengthType::Horizontal);
    auto centerY =
        lengthContext.resolve(radialGradient->getCy(), SVGLengthContext::LengthType::Vertical);
    auto center = Point::Make(centerX, centerY);

    auto radius =
        lengthContext.resolve(radialGradient->getR(), SVGLengthContext::LengthType::Other);

    std::vector<Color> colors;
    std::vector<float> offsets;
    //stop的值不需要用lengthContext解析，因为它是一个百分比值
    for (const auto& node : radialGradient->getChildren()) {
      auto stop = std::static_pointer_cast<SVGStop>(node);
      auto offset = stop->getOffset();
      offsets.push_back(offset.value());
      auto stopColor = stop->getStopColor();
      colors.push_back(stopColor->color());
    }

    auto styleMatrix = radialGradient->getGradientTransform();
    auto gradientStyle = Gradient::MakeRadial(center, radius, colors, offsets);
    gradientStyle->setMatrix(styleMatrix);
    return gradientStyle;
  }

  std::shared_ptr<ShapeStyle> parseLinearGradient(
      const std::shared_ptr<SVGLinearGradient>& linearGradient, SVGLengthContext lengthContext) {

    lengthContext.setBoundingBoxUnits(linearGradient->getGradientUnits());
    auto x1 =
        lengthContext.resolve(linearGradient->getX1(), SVGLengthContext::LengthType::Horizontal);
    auto y1 =
        lengthContext.resolve(linearGradient->getY1(), SVGLengthContext::LengthType::Vertical);
    auto startPoint = Point::Make(x1, y1);

    auto x2 =
        lengthContext.resolve(linearGradient->getX2(), SVGLengthContext::LengthType::Horizontal);
    auto y2 =
        lengthContext.resolve(linearGradient->getY2(), SVGLengthContext::LengthType::Vertical);
    auto endPoint = Point::Make(x2, y2);

    std::vector<Color> colors;
    std::vector<float> offsets;
    for (const auto& node : linearGradient->getChildren()) {
      auto stop = std::static_pointer_cast<SVGStop>(node);
      auto offset = stop->getOffset();
      offsets.push_back(offset.value());
      auto stopColor = stop->getStopColor();
      colors.push_back(stopColor->color());
    }
    auto styleMatrix = linearGradient->getGradientTransform();
    auto gradientStyle = Gradient::MakeLinear(startPoint, endPoint, colors, offsets);
    gradientStyle->setMatrix(styleMatrix);
    return gradientStyle;
  }

  std::shared_ptr<ShapeStyle> parsePattern(const std::shared_ptr<SVGPattern>& pattern,
                                           SVGLengthContext lengthContext) {
    lengthContext.setBoundingBoxUnits(pattern->getPatternUnits());

    auto children = pattern->getChildren();
    if (pattern->getChildren().size() != 1) {
      return SolidColor::Make(Color::Black());
    }
    auto child = children[0];
    std::shared_ptr<SVGImage> imageNode = nullptr;
    Matrix imageMatrix = Matrix::I();
    if (child->tag() == SVGTag::Use) {
      auto useNode = std::static_pointer_cast<SVGUse>(child);
      imageMatrix = useNode->getTransform();
      auto iri = useNode->getHref().iri();
      auto iter = nodeIDMapper_.find(iri);
      if (iter == nodeIDMapper_.end()) {
        return SolidColor::Make(Color::Black());
      }
      auto node = iter->second;
      if (node->tag() == SVGTag::Image) {
        imageNode = std::static_pointer_cast<SVGImage>(node);
      }
    } else if (child->tag() == SVGTag::Image) {
      imageNode = std::static_pointer_cast<SVGImage>(child);
    } else {
      return SolidColor::Make(Color::Black());
    }

    float width =
        lengthContext.resolve(imageNode->getWidth(), SVGLengthContext::LengthType::Horizontal);
    float height =
        lengthContext.resolve(imageNode->getHeight(), SVGLengthContext::LengthType::Vertical);
    Rect imageRect = Rect::MakeWH(width, height);
    imageMatrix.mapRect(&imageRect);

    auto imageInfo = SVGImage::LoadImage(imageNode->getHref(), imageRect);
    if (!imageInfo.image) {
      return SolidColor::Make(Color::Black());
    }
    return ImagePattern::Make(imageInfo.image, TileMode::Repeat, TileMode::Repeat);
  }

  std::shared_ptr<ShapeStyle> parsePaint(SVGProperty<SVGPaint, true> paint,
                                         SVGProperty<SVGNumberType, true> opacity,
                                         const SVGLengthContext& lengthContext) {
    if (!paint.isValue()) {
      return SolidColor::Make(Color::Black());
    }
    float alpha = 1.0f;
    if (opacity.isValue()) {
      alpha = opacity.get().value();
    }
    if (paint->type() == SVGPaint::Type::Color) {
      auto color = paint->color().color();
      color.alpha = alpha;
      return SolidColor::Make(color);
    } else {  //paint->type() == SVGPaint::Type::IRI
      auto iri = paint->iri().iri();
      auto iter = nodeIDMapper_.find(iri);
      if (iter == nodeIDMapper_.end()) {
        return SolidColor::Make(Color::Black());
      }
      auto node = iter->second;
      std::shared_ptr<ShapeStyle> iriStyle = nullptr;
      if (node->tag() == SVGTag::RadialGradient) {
        auto radialGradient = std::static_pointer_cast<SVGRadialGradient>(node);
        iriStyle = parseRadialGradient(radialGradient, lengthContext);
      } else if (node->tag() == SVGTag::LinearGradient) {
        auto linearGradient = std::static_pointer_cast<SVGLinearGradient>(node);
        iriStyle = parseLinearGradient(linearGradient, lengthContext);
      } else if (node->tag() == SVGTag::Pattern) {
        auto pattern = std::static_pointer_cast<SVGPattern>(node);
        iriStyle = parsePattern(pattern, lengthContext);
      }
      iriStyle->setAlpha(alpha);
      return iriStyle;
    }
    return SolidColor::Make(Color::Black());
  };

  Path parseRect(const std::shared_ptr<SVGRect>& rect, const SVGLengthContext& lengthContext) {
    Path resultPath;

    auto x = lengthContext.resolve(rect->getX(), SVGLengthContext::LengthType::Horizontal);
    auto y = lengthContext.resolve(rect->getY(), SVGLengthContext::LengthType::Vertical);
    auto width = lengthContext.resolve(rect->getWidth(), SVGLengthContext::LengthType::Horizontal);
    auto height = lengthContext.resolve(rect->getHeight(), SVGLengthContext::LengthType::Vertical);

    RRect rRect;
    rRect.rect = Rect::MakeXYWH(x, y, width, height);
    if (rect->getRx().has_value()) {
      rRect.radii.x =
          lengthContext.resolve(rect->getRx().value(), SVGLengthContext::LengthType::Horizontal);
    }
    if (rect->getRy().has_value()) {
      rRect.radii.y =
          lengthContext.resolve(rect->getRy().value(), SVGLengthContext::LengthType::Vertical);
    }
    resultPath.addRRect(rRect);
    return resultPath;
  }

  void parseShape(const std::shared_ptr<SVGShape>& shape, const SVGLengthContext& lengthContext) {
    auto shapeLayer = ShapeLayer::Make();
    layerStack_.top()->addChild(shapeLayer);
    layerStack_.push(shapeLayer);
    {
      shapeLayer->setMatrix(shape->getTransform());
      Path path;
      switch (shape->tag()) {
        case SVGTag::Rect: {
          path = parseRect(std::static_pointer_cast<SVGRect>(shape), lengthContext);
          break;
        }
        //TODO add other shapes
        default:
          break;
      }

      SVGLengthContext paintLengthContext(path.getBounds().size());

      if (shape->getFill().isValue()) {
        auto fillStyle = parsePaint(shape->getFill(), shape->getFillOpacity(), paintLengthContext);
        shapeLayer->setFillStyle(fillStyle);
      }

      if (shape->getStroke().isValue()) {
        auto strokeStyle =
            parsePaint(shape->getStroke(), shape->getStrokeOpacity(), paintLengthContext);
        shapeLayer->setStrokeStyle(strokeStyle);

        if (shape->getStrokeWidth().isValue()) {
          shapeLayer->setLineWidth(shape->getStrokeWidth()->value());
        }
        if (shape->getStrokeLineCap().isValue()) {
          //TODO SVGLineCap mapping to LineCap
          // shapeLayer->setLineCap(
          //     static_cast<LineCap>(shape->getStrokeLineCap().get().value_or(SVGLineCap::Butt)));
        }
        if (shape->getStrokeLineJoin().isValue()) {
          //TODO SVGLineJoin mapping to LineJoin
          // shapeLayer->setLineJoin(
          //     static_cast<LineJoin>(shape->getStrokeLineJoin().get().value_or(SVGLineJoin::Miter)));
        }
        if (shape->getStrokeDashArray().isValue()) {
          const auto& svgDashArray = shape->getStrokeDashArray()->dashArray();
          std::vector<float> dashArray;
          dashArray.reserve(svgDashArray.size());
          for (auto dash : svgDashArray) {
            dashArray.push_back(
                paintLengthContext.resolve(dash, SVGLengthContext::LengthType::Other));
          }
        }
        if (shape->getStrokeDashOffset().isValue()) {
          SVGLength offset = shape->getStrokeDashOffset().get().value();
          shapeLayer->setLineDashPhase(
              paintLengthContext.resolve(offset, SVGLengthContext::LengthType::Other));
        }
      }

      //TODO other shape properties

      // rect->getFilter();

      // rect->getClipPath();
      // rect->getClipRule();

      // rect->getMask();

      // rect->getOpacity();

      // rect->getVisibility();
      // rect->getDisplay();
    }
    layerStack_.pop();
  }

  void parseNode(const std::shared_ptr<SVGNode>& node, const SVGLengthContext& lengthContext) {
    if (node->tag() == SVGTag::Rect || node->tag() == SVGTag::Circle ||
        node->tag() == SVGTag::Ellipse) {
      auto rectNode = std::static_pointer_cast<SVGRect>(node);
      parseRect(rectNode, lengthContext);
    }

    switch (node->tag()) {
      case SVGTag::Rect:
      case SVGTag::Circle:
      case SVGTag::Ellipse:
      case SVGTag::Line:
      case SVGTag::Polygon:
      case SVGTag::Polyline:
      case SVGTag::Path: {
        auto shapeNode = std::static_pointer_cast<SVGShape>(node);
        parseShape(shapeNode, lengthContext);
        break;
      }
      case SVGTag::G: {
        auto groupNode = std::static_pointer_cast<SVGGroup>(node);
        auto groupLayer = Layer::Make();
        layerStack_.top()->addChild(groupLayer);
        layerStack_.push(groupLayer);
        for (const auto& child : groupNode->getChildren()) {
          parseNode(child, lengthContext);
        }
        layerStack_.pop();
        break;
      }
      case SVGTag::Image: {
        break;
      }
      default:
        break;
    }
  }

 private:
  std::shared_ptr<SVGRoot> root_;
  const SVGIDMapper& nodeIDMapper_;
  std::stack<std::shared_ptr<Layer>> layerStack_;
};

// TGFX_TEST(SVGParseTest, SVGParse) {
//   /*
//   <svg width="100" height="100" viewBox="0 0 100 100" fill="none" xmlns="http://www.w3.org/2000/svg">
//     <rect width="100" height="100" fill="url(#paint0_radial_88_7)"/>
//     <defs>
//       <radialGradient id="paint0_radial_88_7" cx="0" cy="0" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(50 50) rotate(90) scale(50)">
//         <stop stop-color="#0800FF"/>
//         <stop offset="0.49" stop-color="#D2E350"/>
//         <stop offset="1" stop-color="#74D477"/>
//       </radialGradient>
//     </defs>
//   </svg>
//   */

//   auto stream =
//       Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/SVG/radialGradient.svg"));
//   EXPECT_TRUE(stream != nullptr);
//   auto svgDOM = SVGDOM::Make(*stream);
//   auto root = svgDOM->getRoot();
//   auto nodeIDMap = svgDOM->nodeIDMapper();

//   auto children = root->getChildren();
//   // EXPECT_EQ(children.size(), 1U);
//   auto childNode0 = children[0];
//   if (childNode0->tag() == SVGTag::Rect) {
//     auto rectNode = std::static_pointer_cast<SVGRect>(childNode0);
//     rectNode->getWidth();
//     rectNode->getHeight();
//     // SVGProperty<SVGPaint, true> svgPaint =rectNode->getFill();
//     auto svgPaint = rectNode->getFill();
//     if (svgPaint.isValue()) {
//       if (svgPaint->type() == SVGPaint::Type::Color) {
//         //normal color
//         auto color = svgPaint->color();
//       } else if (svgPaint->type() == SVGPaint::Type::IRI) {
//         auto iri = svgPaint->iri();
//         const auto& nodeID = iri.iri();
//         auto iter = nodeIDMap.find(nodeID);
//         if (iter != nodeIDMap.end() && iter->second) {
//           if (iter->second->tag() == SVGTag::RadialGradient) {
//             auto radialGradient = std::static_pointer_cast<SVGRadialGradient>(iter->second);
//             radialGradient->getGradientTransform();
//             radialGradient->getGradientUnits();
//             auto center =
//                 Point::Make(radialGradient->getCx().value(), radialGradient->getCy().value());
//             auto radius = radialGradient->getR();

//             std::vector<Color> colors;
//             std::vector<float> offsets;
//             for (const auto& node : radialGradient->getChildren()) {
//               auto stop = std::static_pointer_cast<SVGStop>(node);
//               auto offset = stop->getOffset();
//               offsets.push_back(offset.value());
//               auto stopColor = stop->getStopColor();
//               colors.push_back(stopColor->color());
//             }
//             Shader::MakeRadialGradient(center, radius.value(), colors, offsets);
//           }
//         }
//       }
//     }
//   }
// }
//

}  // namespace tgfx