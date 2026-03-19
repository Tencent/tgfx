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

#include "LayerTreeParser.h"
#include <fstream>
#include "nlohmann/json.hpp"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/MeshLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/filters/BlendFilter.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/InnerShadowFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/MergePath.h"
#include "tgfx/layers/vectors/Polystar.h"
#include "tgfx/layers/vectors/Rectangle.h"
#include "tgfx/layers/vectors/Repeater.h"
#include "tgfx/layers/vectors/RoundCorner.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/TrimPath.h"
#include "tgfx/layers/vectors/VectorGroup.h"
#include "tgfx/platform/Print.h"
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

using json = nlohmann::json;

// Compatibility: newer tgfx uses setCenter(), older uses setPosition().
template <typename T, typename = void>
struct HasSetCenter : std::false_type {};
template <typename T>
struct HasSetCenter<T, std::void_t<decltype(std::declval<T>().setCenter(Point::Zero()))>>
    : std::true_type {};

template <typename T>
inline void SetPosition(T* obj, const Point& pt) {
  if constexpr (HasSetCenter<T>::value) {
    obj->setCenter(pt);
  } else {
    obj->setPosition(pt);
  }
}

// ========================= Basic type parsers =========================

static std::shared_ptr<Layer> CreateLayerByType(const std::string& typeStr) {
  if (typeStr == "Vector") return VectorLayer::Make();
  if (typeStr == "Shape") return ShapeLayer::Make();
  if (typeStr == "Solid") return SolidLayer::Make();
  if (typeStr == "Image") return ImageLayer::Make();
  if (typeStr == "Text") return TextLayer::Make();
  if (typeStr == "Mesh") return MeshLayer::Make();
  return Layer::Make();
}

static BlendMode ParseBlendMode(const std::string& str) {
  if (str == "Clear") return BlendMode::Clear;
  if (str == "Src") return BlendMode::Src;
  if (str == "Dst") return BlendMode::Dst;
  if (str == "SrcOver") return BlendMode::SrcOver;
  if (str == "DstOver") return BlendMode::DstOver;
  if (str == "SrcIn") return BlendMode::SrcIn;
  if (str == "DstIn") return BlendMode::DstIn;
  if (str == "SrcOut") return BlendMode::SrcOut;
  if (str == "DstOut") return BlendMode::DstOut;
  if (str == "SrcATop") return BlendMode::SrcATop;
  if (str == "DstATop") return BlendMode::DstATop;
  if (str == "Xor") return BlendMode::Xor;
  if (str == "PlusLighter") return BlendMode::PlusLighter;
  if (str == "Modulate") return BlendMode::Modulate;
  if (str == "Screen") return BlendMode::Screen;
  if (str == "Overlay") return BlendMode::Overlay;
  if (str == "Darken") return BlendMode::Darken;
  if (str == "Lighten") return BlendMode::Lighten;
  if (str == "ColorDodge") return BlendMode::ColorDodge;
  if (str == "ColorBurn") return BlendMode::ColorBurn;
  if (str == "HardLight") return BlendMode::HardLight;
  if (str == "SoftLight") return BlendMode::SoftLight;
  if (str == "Difference") return BlendMode::Difference;
  if (str == "Exclusion") return BlendMode::Exclusion;
  if (str == "Multiply") return BlendMode::Multiply;
  if (str == "Hue") return BlendMode::Hue;
  if (str == "Saturation") return BlendMode::Saturation;
  if (str == "Color") return BlendMode::Color;
  if (str == "Luminosity") return BlendMode::Luminosity;
  if (str == "PlusDarker") return BlendMode::PlusDarker;
  return BlendMode::SrcOver;
}

static LayerMaskType ParseMaskType(const std::string& str) {
  if (str == "Contour") return LayerMaskType::Contour;
  if (str == "Luminance") return LayerMaskType::Luminance;
  return LayerMaskType::Alpha;
}

static TileMode ParseTileMode(const std::string& str) {
  if (str == "Clamp") return TileMode::Clamp;
  if (str == "Repeat") return TileMode::Repeat;
  if (str == "Mirror") return TileMode::Mirror;
  if (str == "Decal") return TileMode::Decal;
  return TileMode::Clamp;
}

static StrokeAlign ParseStrokeAlign(const std::string& str) {
  if (str == "Inside") return StrokeAlign::Inside;
  if (str == "Outside") return StrokeAlign::Outside;
  return StrokeAlign::Center;
}

static FillRule ParseFillRule(const std::string& str) {
  if (str == "EvenOdd") return FillRule::EvenOdd;
  return FillRule::Winding;
}

static LayerPlacement ParseLayerPlacement(const std::string& str) {
  if (str == "Foreground") return LayerPlacement::Foreground;
  return LayerPlacement::Background;
}

static MergePathOp ParseMergePathOp(const std::string& str) {
  if (str == "Union") return MergePathOp::Union;
  if (str == "Difference") return MergePathOp::Difference;
  if (str == "Intersect") return MergePathOp::Intersect;
  if (str == "XOR") return MergePathOp::XOR;
  return MergePathOp::Append;
}

static TrimPathType ParseTrimPathType(const std::string& str) {
  if (str == "Continuous") return TrimPathType::Continuous;
  return TrimPathType::Separate;
}

static PolystarType ParsePolystarType(const std::string& str) {
  if (str == "Polygon") return PolystarType::Polygon;
  return PolystarType::Star;
}

static RepeaterOrder ParseRepeaterOrder(const std::string& str) {
  if (str == "AboveOriginal") return RepeaterOrder::AboveOriginal;
  return RepeaterOrder::BelowOriginal;
}

static Matrix3D ParseMatrix3D(const json& j) {
  auto matrix = Matrix3D();
  for (size_t row = 0; row < 4; row++) {
    for (size_t col = 0; col < 4; col++) {
      matrix.setRowColumn(static_cast<int>(row), static_cast<int>(col), j[row][col].get<float>());
    }
  }
  return matrix;
}

static Rect ParseRect(const json& j) {
  return Rect::MakeLTRB(j["left"].get<float>(), j["top"].get<float>(), j["right"].get<float>(),
                        j["bottom"].get<float>());
}

static Point ParsePoint(const json& j) {
  return Point::Make(j["x"].get<float>(), j["y"].get<float>());
}

static Color ParseColor(const json& j) {
  auto color = Color();
  color.red = j["red"].get<float>();
  color.green = j["green"].get<float>();
  color.blue = j["blue"].get<float>();
  color.alpha = j["alpha"].get<float>();
  return color;
}

static bool IsEmptyRect(const Rect& rect) {
  return rect.left == 0.f && rect.top == 0.f && rect.right == 0.f && rect.bottom == 0.f;
}

// ========================= Shader parsing (for ShapeStyle) =========================

static std::shared_ptr<Shader> ParseShader(const json& j) {
  if (j.is_null()) return nullptr;
  auto shaderType = j["shaderType"].get<std::string>();
  if (shaderType == "Color") {
    auto color = ParseColor(j["color"]);
    return Shader::MakeColorShader(color);
  }
  if (shaderType == "Gradient") {
    auto gradType = j["gradientType"].get<std::string>();
    std::vector<Color> colors = {};
    for (const auto& c : j["colors"]) {
      colors.push_back(ParseColor(c));
    }
    std::vector<float> positions = {};
    for (const auto& p : j["positions"]) {
      positions.push_back(p.get<float>());
    }
    if (gradType == "Linear") {
      auto startPt = ParsePoint(j["points"][0]);
      auto endPt = ParsePoint(j["points"][1]);
      return Shader::MakeLinearGradient(startPt, endPt, colors, positions);
    }
    if (gradType == "Radial") {
      auto center = ParsePoint(j["points"][0]);
      auto radius = j["radiuses"][0].get<float>();
      return Shader::MakeRadialGradient(center, radius, colors, positions);
    }
    if (gradType == "Conic") {
      auto center = ParsePoint(j["points"][0]);
      auto startAngle = j["radiuses"][0].get<float>();
      auto endAngle = j["radiuses"][1].get<float>();
      return Shader::MakeConicGradient(center, startAngle, endAngle, colors, positions);
    }
    if (gradType == "Diamond") {
      auto center = ParsePoint(j["points"][0]);
      auto radius = j["radiuses"][0].get<float>();
      return Shader::MakeDiamondGradient(center, radius, colors, positions);
    }
  }
  return nullptr;
}

// ========================= ColorSource parsing (for VectorElement) =========================

static std::shared_ptr<ColorSource> ParseColorSource(const json& j) {
  if (j.is_null()) return nullptr;
  auto sourceType = j["colorSourceType"].get<std::string>();
  if (sourceType == "SolidColor") {
    auto color = ParseColor(j["color"]);
    return SolidColor::Make(color);
  }
  if (sourceType == "Gradient") {
    auto gradType = j["gradientType"].get<std::string>();
    std::vector<Color> colors = {};
    for (const auto& c : j["colors"]) {
      colors.push_back(ParseColor(c));
    }
    std::vector<float> positions = {};
    for (const auto& p : j["positions"]) {
      positions.push_back(p.get<float>());
    }
    std::shared_ptr<Gradient> gradient = nullptr;
    if (gradType == "Linear") {
      auto startPt = ParsePoint(j["startPoint"]);
      auto endPt = ParsePoint(j["endPoint"]);
      gradient = Gradient::MakeLinear(startPt, endPt, colors, positions);
    } else if (gradType == "Radial") {
      auto center = ParsePoint(j["center"]);
      auto radius = j["radius"].get<float>();
      gradient = Gradient::MakeRadial(center, radius, colors, positions);
    } else if (gradType == "Conic") {
      auto center = ParsePoint(j["center"]);
      auto startAngle = j["startAngle"].get<float>();
      auto endAngle = j["endAngle"].get<float>();
      gradient = Gradient::MakeConic(center, startAngle, endAngle, colors, positions);
    } else if (gradType == "Diamond") {
      auto center = ParsePoint(j["center"]);
      auto radius = j["radius"].get<float>();
      gradient = Gradient::MakeDiamond(center, radius, colors, positions);
    }
    if (gradient && j.contains("matrix")) {
      const auto& mj = j["matrix"];
      auto matrix = Matrix();
      matrix.setAll(mj[0].get<float>(), mj[1].get<float>(), mj[2].get<float>(), mj[3].get<float>(),
                    mj[4].get<float>(), mj[5].get<float>());
      gradient->setMatrix(matrix);
    }
    return gradient;
  }
  return nullptr;
}

// ========================= ShapeStyle parsing =========================

static std::shared_ptr<ShapeStyle> ParseShapeStyle(const json& j) {
  auto color = ParseColor(j["color"]);
  auto blendMode = ParseBlendMode(j["blendMode"].get<std::string>());
  auto shader = ParseShader(j["shader"]);
  if (shader) {
    return ShapeStyle::Make(std::move(shader), color.alpha, blendMode);
  }
  return ShapeStyle::Make(color, blendMode);
}

// ========================= VectorElement parsing =========================

static std::shared_ptr<VectorElement> ParseVectorElement(const json& j);

static std::vector<std::shared_ptr<VectorElement>> ParseVectorElements(const json& j) {
  std::vector<std::shared_ptr<VectorElement>> elements = {};
  for (const auto& item : j) {
    auto element = ParseVectorElement(item);
    if (element) {
      elements.push_back(element);
    }
  }
  return elements;
}

static std::shared_ptr<VectorElement> ParseVectorElement(const json& j) {
  auto elementType = j["elementType"].get<std::string>();
  auto enabled = j.value("enabled", true);

  if (elementType == "Rectangle") {
    auto rect = Rectangle::Make();
    rect->setEnabled(enabled);
    SetPosition(rect.get(), ParsePoint(j["position"]));
    auto size = Size::Make(j["size"]["width"].get<float>(), j["size"]["height"].get<float>());
    rect->setSize(size);
    rect->setRoundness(j.value("roundness", 0.0f));
    rect->setReversed(j.value("reversed", false));
    return rect;
  }
  if (elementType == "Ellipse") {
    auto ellipse = Ellipse::Make();
    ellipse->setEnabled(enabled);
    ellipse->setPosition(ParsePoint(j["position"]));
    auto size = Size::Make(j["size"]["width"].get<float>(), j["size"]["height"].get<float>());
    ellipse->setSize(size);
    ellipse->setReversed(j.value("reversed", false));
    return ellipse;
  }
  if (elementType == "Polystar") {
    auto polystar = Polystar::Make();
    polystar->setEnabled(enabled);
    SetPosition(polystar.get(), ParsePoint(j["position"]));
    polystar->setPolystarType(ParsePolystarType(j.value("polystarType", "Star")));
    polystar->setPointCount(j.value("pointCount", 5.0f));
    polystar->setRotation(j.value("rotation", 0.0f));
    polystar->setOuterRadius(j.value("outerRadius", 100.0f));
    polystar->setOuterRoundness(j.value("outerRoundness", 0.0f));
    polystar->setInnerRadius(j.value("innerRadius", 50.0f));
    polystar->setInnerRoundness(j.value("innerRoundness", 0.0f));
    polystar->setReversed(j.value("reversed", false));
    return polystar;
  }
  if (elementType == "ShapePath") {
    auto shapePath = ShapePath::Make();
    shapePath->setEnabled(enabled);
    auto pathStr = j.value("path", "");
    if (!pathStr.empty()) {
      auto path = SVGPathParser::FromSVGString(pathStr);
      if (path) {
        shapePath->setPath(*path);
      }
    }
    shapePath->setReversed(j.value("reversed", false));
    return shapePath;
  }
  if (elementType == "FillStyle") {
    auto colorSource = ParseColorSource(j["colorSource"]);
    if (!colorSource) return nullptr;
    auto fill = FillStyle::Make(std::move(colorSource));
    if (!fill) return nullptr;
    fill->setEnabled(enabled);
    fill->setAlpha(j.value("alpha", 1.0f));
    if (j.contains("blendMode")) {
      fill->setBlendMode(ParseBlendMode(j["blendMode"].get<std::string>()));
    }
    if (j.contains("fillRule")) {
      fill->setFillRule(ParseFillRule(j["fillRule"].get<std::string>()));
    }
    if (j.contains("placement")) {
      fill->setPlacement(ParseLayerPlacement(j["placement"].get<std::string>()));
    }
    return fill;
  }
  if (elementType == "StrokeStyle") {
    auto colorSource = ParseColorSource(j["colorSource"]);
    if (!colorSource) return nullptr;
    auto stroke = StrokeStyle::Make(std::move(colorSource));
    if (!stroke) return nullptr;
    stroke->setEnabled(enabled);
    stroke->setAlpha(j.value("alpha", 1.0f));
    if (j.contains("blendMode")) {
      stroke->setBlendMode(ParseBlendMode(j["blendMode"].get<std::string>()));
    }
    stroke->setStrokeWidth(j.value("strokeWidth", 1.0f));
    stroke->setLineCap(static_cast<LineCap>(j.value("lineCap", 0)));
    stroke->setLineJoin(static_cast<LineJoin>(j.value("lineJoin", 0)));
    stroke->setMiterLimit(j.value("miterLimit", 4.0f));
    stroke->setDashAdaptive(j.value("dashAdaptive", false));
    stroke->setDashOffset(j.value("dashOffset", 0.0f));
    if (j.contains("strokeAlign")) {
      stroke->setStrokeAlign(ParseStrokeAlign(j["strokeAlign"].get<std::string>()));
    }
    if (j.contains("placement")) {
      stroke->setPlacement(ParseLayerPlacement(j["placement"].get<std::string>()));
    }
    if (j.contains("dashes")) {
      std::vector<float> dashes = {};
      for (const auto& d : j["dashes"]) {
        dashes.push_back(d.get<float>());
      }
      stroke->setDashes(std::move(dashes));
    }
    return stroke;
  }
  if (elementType == "TrimPath") {
    auto trim = TrimPath::Make();
    trim->setEnabled(enabled);
    trim->setStart(j.value("start", 0.0f));
    trim->setEnd(j.value("end", 1.0f));
    trim->setOffset(j.value("offset", 0.0f));
    if (j.contains("trimType")) {
      trim->setTrimType(ParseTrimPathType(j["trimType"].get<std::string>()));
    }
    return trim;
  }
  if (elementType == "RoundCorner") {
    auto rc = RoundCorner::Make();
    rc->setEnabled(enabled);
    rc->setRadius(j.value("radius", 10.0f));
    return rc;
  }
  if (elementType == "MergePath") {
    auto mp = MergePath::Make();
    mp->setEnabled(enabled);
    if (j.contains("mode")) {
      mp->setMode(ParseMergePathOp(j["mode"].get<std::string>()));
    }
    return mp;
  }
  if (elementType == "Repeater") {
    auto rep = Repeater::Make();
    rep->setEnabled(enabled);
    rep->setCopies(j.value("copies", 3.0f));
    rep->setOffset(j.value("offset", 0.0f));
    if (j.contains("order")) {
      rep->setOrder(ParseRepeaterOrder(j["order"].get<std::string>()));
    }
    if (j.contains("anchor")) rep->setAnchor(ParsePoint(j["anchor"]));
    if (j.contains("position")) rep->setPosition(ParsePoint(j["position"]));
    rep->setRotation(j.value("rotation", 0.0f));
    if (j.contains("scale")) rep->setScale(ParsePoint(j["scale"]));
    rep->setStartAlpha(j.value("startAlpha", 1.0f));
    rep->setEndAlpha(j.value("endAlpha", 1.0f));
    return rep;
  }
  if (elementType == "VectorGroup") {
    auto group = VectorGroup::Make();
    group->setEnabled(enabled);
    if (j.contains("anchor")) group->setAnchor(ParsePoint(j["anchor"]));
    if (j.contains("position")) group->setPosition(ParsePoint(j["position"]));
    if (j.contains("scale")) group->setScale(ParsePoint(j["scale"]));
    group->setRotation(j.value("rotation", 0.0f));
    group->setAlpha(j.value("alpha", 1.0f));
    group->setSkew(j.value("skew", 0.0f));
    group->setSkewAxis(j.value("skewAxis", 0.0f));
    if (j.contains("elements")) {
      group->setElements(ParseVectorElements(j["elements"]));
    }
    return group;
  }
  return nullptr;
}

// ========================= LayerFilter parsing =========================

static std::vector<std::shared_ptr<LayerFilter>> ParseFilters(const json& j) {
  std::vector<std::shared_ptr<LayerFilter>> filters = {};
  for (const auto& fj : j) {
    auto filterType = fj["filterType"].get<std::string>();
    if (filterType == "DropShadow") {
      auto f =
          DropShadowFilter::Make(fj["offsetX"].get<float>(), fj["offsetY"].get<float>(),
                                 fj["blurrinessX"].get<float>(), fj["blurrinessY"].get<float>(),
                                 ParseColor(fj["color"]), fj.value("dropsShadowOnly", false));
      filters.push_back(std::move(f));
    } else if (filterType == "InnerShadow") {
      auto f =
          InnerShadowFilter::Make(fj["offsetX"].get<float>(), fj["offsetY"].get<float>(),
                                  fj["blurrinessX"].get<float>(), fj["blurrinessY"].get<float>(),
                                  ParseColor(fj["color"]), fj.value("innerShadowOnly", false));
      filters.push_back(std::move(f));
    } else if (filterType == "Blur") {
      auto tileMode = ParseTileMode(fj.value("tileMode", "Decal"));
      auto f = BlurFilter::Make(fj["blurrinessX"].get<float>(), fj["blurrinessY"].get<float>(),
                                tileMode);
      filters.push_back(std::move(f));
    } else if (filterType == "Blend") {
      auto color = ParseColor(fj["color"]);
      auto mode = ParseBlendMode(fj["blendMode"].get<std::string>());
      auto f = BlendFilter::Make(color, mode);
      filters.push_back(std::move(f));
    } else if (filterType == "ColorMatrix") {
      std::array<float, 20> matrix = {};
      const auto& mj = fj["matrix"];
      for (size_t i = 0; i < 20 && i < mj.size(); ++i) {
        matrix[i] = mj[i].get<float>();
      }
      auto f = ColorMatrixFilter::Make(matrix);
      filters.push_back(std::move(f));
    }
  }
  return filters;
}

// ========================= LayerStyle parsing =========================

static std::vector<std::shared_ptr<LayerStyle>> ParseLayerStyles(const json& j) {
  std::vector<std::shared_ptr<LayerStyle>> styles = {};
  for (const auto& sj : j) {
    auto styleType = sj["styleType"].get<std::string>();
    std::shared_ptr<LayerStyle> style = nullptr;
    if (styleType == "DropShadow") {
      auto s = DropShadowStyle::Make(sj["offsetX"].get<float>(), sj["offsetY"].get<float>(),
                                     sj["blurrinessX"].get<float>(), sj["blurrinessY"].get<float>(),
                                     ParseColor(sj["color"]), sj.value("showBehindLayer", true));
      style = std::move(s);
    } else if (styleType == "InnerShadow") {
      auto s = InnerShadowStyle::Make(sj["offsetX"].get<float>(), sj["offsetY"].get<float>(),
                                      sj["blurrinessX"].get<float>(),
                                      sj["blurrinessY"].get<float>(), ParseColor(sj["color"]));
      style = std::move(s);
    } else if (styleType == "BackgroundBlur") {
      auto tileMode = ParseTileMode(sj.value("tileMode", "Mirror"));
      auto s = BackgroundBlurStyle::Make(sj["blurrinessX"].get<float>(),
                                         sj["blurrinessY"].get<float>(), tileMode);
      style = std::move(s);
    }
    if (style) {
      if (sj.contains("blendMode")) {
        style->setBlendMode(ParseBlendMode(sj["blendMode"].get<std::string>()));
      }
      if (sj.contains("excludeChildEffects")) {
        style->setExcludeChildEffects(sj["excludeChildEffects"].get<bool>());
      }
      styles.push_back(std::move(style));
    }
  }
  return styles;
}

// ========================= Layer subclass property parsers =========================

static void ParseSolidLayerProps(const json& j, const std::shared_ptr<Layer>& layer) {
  auto solid = std::static_pointer_cast<SolidLayer>(layer);
  if (j.contains("solidWidth")) solid->setWidth(j["solidWidth"].get<float>());
  if (j.contains("solidHeight")) solid->setHeight(j["solidHeight"].get<float>());
  if (j.contains("solidRadiusX")) solid->setRadiusX(j["solidRadiusX"].get<float>());
  if (j.contains("solidRadiusY")) solid->setRadiusY(j["solidRadiusY"].get<float>());
  if (j.contains("solidColor")) solid->setColor(ParseColor(j["solidColor"]));
}

static void ParseShapeLayerProps(const json& j, const std::shared_ptr<Layer>& layer) {
  auto shape = std::static_pointer_cast<ShapeLayer>(layer);
  if (j.contains("shapePath")) {
    auto pathStr = j["shapePath"].get<std::string>();
    if (!pathStr.empty()) {
      auto path = SVGPathParser::FromSVGString(pathStr);
      if (path) {
        shape->setPath(*path);
      }
    }
  }
  if (j.contains("lineWidth")) shape->setLineWidth(j["lineWidth"].get<float>());
  if (j.contains("lineCap")) shape->setLineCap(static_cast<LineCap>(j["lineCap"].get<int>()));
  if (j.contains("lineJoin")) shape->setLineJoin(static_cast<LineJoin>(j["lineJoin"].get<int>()));
  if (j.contains("miterLimit")) shape->setMiterLimit(j["miterLimit"].get<float>());
  if (j.contains("lineDashPhase")) shape->setLineDashPhase(j["lineDashPhase"].get<float>());
  if (j.contains("lineDashAdaptive")) shape->setLineDashAdaptive(j["lineDashAdaptive"].get<bool>());
  if (j.contains("strokeAlign")) {
    shape->setStrokeAlign(ParseStrokeAlign(j["strokeAlign"].get<std::string>()));
  }
  if (j.contains("strokeOnTop")) shape->setStrokeOnTop(j["strokeOnTop"].get<bool>());
  if (j.contains("lineDashPattern")) {
    std::vector<float> pattern = {};
    for (const auto& v : j["lineDashPattern"]) {
      pattern.push_back(v.get<float>());
    }
    shape->setLineDashPattern(pattern);
  }
  if (j.contains("fillStyles")) {
    std::vector<std::shared_ptr<ShapeStyle>> fills = {};
    for (const auto& sj : j["fillStyles"]) {
      fills.push_back(ParseShapeStyle(sj));
    }
    shape->setFillStyles(std::move(fills));
  }
  if (j.contains("strokeStyles")) {
    std::vector<std::shared_ptr<ShapeStyle>> strokes = {};
    for (const auto& sj : j["strokeStyles"]) {
      strokes.push_back(ParseShapeStyle(sj));
    }
    shape->setStrokeStyles(std::move(strokes));
  }
}

static void ParseTextLayerProps(const json& j, const std::shared_ptr<Layer>& layer) {
  auto text = std::static_pointer_cast<TextLayer>(layer);
  if (j.contains("text")) text->setText(j["text"].get<std::string>());
  if (j.contains("textColor")) text->setTextColor(ParseColor(j["textColor"]));
  if (j.contains("textWidth")) text->setWidth(j["textWidth"].get<float>());
  if (j.contains("textHeight")) text->setHeight(j["textHeight"].get<float>());
  if (j.contains("textAlign")) {
    text->setTextAlign(static_cast<TextAlign>(j["textAlign"].get<int>()));
  }
  if (j.contains("autoWrap")) text->setAutoWrap(j["autoWrap"].get<bool>());
  // Font is partially restored (size only; typeface requires runtime lookup)
  if (j.contains("fontSize")) {
    auto font = text->font();
    auto newFont = Font(font.getTypeface(), j["fontSize"].get<float>());
    text->setFont(newFont);
  }
}

static void ParseVectorLayerProps(const json& j, const std::shared_ptr<Layer>& layer) {
  auto vector = std::static_pointer_cast<VectorLayer>(layer);
  if (j.contains("contents")) {
    auto contents = ParseVectorElements(j["contents"]);
    vector->setContents(std::move(contents));
  }
}

// ========================= Main layer parser =========================

static std::shared_ptr<Layer> ParseLayer(const json& j) {
  auto typeStr = j["type"].get<std::string>();
  auto layer = CreateLayerByType(typeStr);

  // name
  auto name = j["name"].get<std::string>();
  if (!name.empty()) {
    layer->setName(name);
  }

  // alpha
  layer->setAlpha(j["alpha"].get<float>());

  // visible
  layer->setVisible(j["visible"].get<bool>());

  // blendMode
  layer->setBlendMode(ParseBlendMode(j["blendMode"].get<std::string>()));

  // matrix3D (contains full transform including position)
  layer->setMatrix3D(ParseMatrix3D(j["matrix3D"]));

  // preserve3D
  layer->setPreserve3D(j["preserve3D"].get<bool>());

  // allowsEdgeAntialiasing
  layer->setAllowsEdgeAntialiasing(j["allowsEdgeAntialiasing"].get<bool>());

  // allowsGroupOpacity
  layer->setAllowsGroupOpacity(j["allowsGroupOpacity"].get<bool>());

  // passThroughBackground
  layer->setPassThroughBackground(j["passThroughBackground"].get<bool>());

  // scrollRect
  auto scrollRect = ParseRect(j["scrollRect"]);
  if (!IsEmptyRect(scrollRect)) {
    layer->setScrollRect(scrollRect);
  }

  // maskType
  layer->setMaskType(ParseMaskType(j["maskType"].get<std::string>()));

  // Filters (full deserialization)
  if (j.contains("filters") && j["filters"].is_array() && !j["filters"].empty()) {
    layer->setFilters(ParseFilters(j["filters"]));
  }

  // Layer styles (full deserialization)
  if (j.contains("layerStyles") && j["layerStyles"].is_array() && !j["layerStyles"].empty()) {
    layer->setLayerStyles(ParseLayerStyles(j["layerStyles"]));
  }

  // Layer subclass-specific properties
  if (typeStr == "Solid") {
    ParseSolidLayerProps(j, layer);
  } else if (typeStr == "Shape") {
    ParseShapeLayerProps(j, layer);
  } else if (typeStr == "Text") {
    ParseTextLayerProps(j, layer);
  } else if (typeStr == "Vector") {
    ParseVectorLayerProps(j, layer);
  }

  // children
  const auto& childrenJson = j["children"];
  std::vector<std::shared_ptr<Layer>> childLayers = {};
  for (const auto& childJson : childrenJson) {
    auto child = ParseLayer(childJson);
    if (child) {
      childLayers.push_back(child);
    }
  }
  for (const auto& child : childLayers) {
    layer->addChild(child);
  }

  return layer;
}

static void ResolveMasks(const json& j, const std::shared_ptr<Layer>& layer) {
  const auto& childrenJson = j["children"];
  const auto& children = layer->children();
  for (size_t i = 0; i < childrenJson.size(); i++) {
    const auto& childJson = childrenJson[i];
    // If this child has hasMask=true and has a previous sibling, use the previous sibling as mask
    if (childJson["hasMask"].get<bool>() && i > 0) {
      children[i]->setMask(children[i - 1]);
    }
    // Recursively resolve masks for this child's subtree
    if (!childJson["children"].empty()) {
      ResolveMasks(childJson, children[i]);
    }
  }
}

std::shared_ptr<Layer> LayerTreeParser::ParseTree(const std::string& jsonPath) {
  std::ifstream file(jsonPath);
  if (!file.is_open()) {
    PrintError("LayerTreeParser: Failed to open file: %s", jsonPath.c_str());
    return nullptr;
  }

  json doc = {};
  try {
    file >> doc;
  } catch (const json::parse_error& e) {
    PrintError("LayerTreeParser: JSON parse error: %s", e.what());
    return nullptr;
  }

  if (!doc.contains("root")) {
    PrintError("LayerTreeParser: JSON missing 'root' field");
    return nullptr;
  }

  auto root = ParseLayer(doc["root"]);
  if (root) {
    ResolveMasks(doc["root"], root);
  }
  return root;
}

}  // namespace tgfx
