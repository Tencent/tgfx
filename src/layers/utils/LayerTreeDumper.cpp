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

#include "LayerTreeDumper.h"
#include <iomanip>
#include "core/shaders/ColorShader.h"
#include "core/shaders/GradientShader.h"
#include "core/utils/Log.h"
#include "core/utils/Types.h"
#include "tgfx/layers/ImageLayer.h"
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
#include "tgfx/layers/vectors/ImagePattern.h"
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
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

// Compatibility: newer tgfx uses center()/setCenter(), older uses position()/setPosition().
template <typename T, typename = void>
struct HasCenter : std::false_type {};
template <typename T>
struct HasCenter<T, std::void_t<decltype(std::declval<const T>().center())>> : std::true_type {};

template <typename T>
inline auto GetPosition(const T* obj) -> const Point& {
  if constexpr (HasCenter<T>::value) {
    return obj->center();
  } else {
    return obj->position();
  }
}

static inline std::string TileModeStr(TileMode mode) {
  switch (mode) {
    case TileMode::Clamp:
      return "Clamp";
    case TileMode::Repeat:
      return "Repeat";
    case TileMode::Mirror:
      return "Mirror";
    case TileMode::Decal:
      return "Decal";
    default:
      return "Clamp";
  }
}

static inline std::string GradientTypeStr(GradientType type) {
  switch (type) {
    case GradientType::Linear:
      return "Linear";
    case GradientType::Radial:
      return "Radial";
    case GradientType::Conic:
      return "Conic";
    case GradientType::Diamond:
      return "Diamond";
    default:
      return "None";
  }
}

static inline std::string MergePathOpStr(MergePathOp op) {
  switch (op) {
    case MergePathOp::Append:
      return "Append";
    case MergePathOp::Union:
      return "Union";
    case MergePathOp::Difference:
      return "Difference";
    case MergePathOp::Intersect:
      return "Intersect";
    case MergePathOp::XOR:
      return "XOR";
    default:
      return "Append";
  }
}

static inline std::string TrimPathTypeStr(TrimPathType type) {
  return type == TrimPathType::Separate ? "Separate" : "Continuous";
}

static inline std::string PolystarTypeStr(PolystarType type) {
  return type == PolystarType::Star ? "Star" : "Polygon";
}

static inline std::string RepeaterOrderStr(RepeaterOrder order) {
  return order == RepeaterOrder::BelowOriginal ? "BelowOriginal" : "AboveOriginal";
}

static inline std::string FillRuleStr(FillRule rule) {
  return rule == FillRule::Winding ? "Winding" : "EvenOdd";
}

static inline std::string StrokeAlignStr(StrokeAlign align) {
  switch (align) {
    case StrokeAlign::Center:
      return "Center";
    case StrokeAlign::Inside:
      return "Inside";
    case StrokeAlign::Outside:
      return "Outside";
    default:
      return "Center";
  }
}

static inline std::string LayerPlacementStr(LayerPlacement placement) {
  return placement == LayerPlacement::Background ? "Background" : "Foreground";
}

// ========================= String/JSON helpers =========================

std::string LayerTreeDumper::LayerTypeToString(LayerType type) {
  switch (type) {
    case LayerType::Layer:
      return "Layer";
    case LayerType::Image:
      return "Image";
    case LayerType::Shape:
      return "Shape";
    case LayerType::Text:
      return "Text";
    case LayerType::Solid:
      return "Solid";
    case LayerType::Vector:
      return "Vector";
    case LayerType::Mesh:
      return "Mesh";
    default:
      return "Unknown";
  }
}

std::string LayerTreeDumper::BlendModeToString(BlendMode mode) {
  switch (mode) {
    case BlendMode::Clear:
      return "Clear";
    case BlendMode::Src:
      return "Src";
    case BlendMode::Dst:
      return "Dst";
    case BlendMode::SrcOver:
      return "SrcOver";
    case BlendMode::DstOver:
      return "DstOver";
    case BlendMode::SrcIn:
      return "SrcIn";
    case BlendMode::DstIn:
      return "DstIn";
    case BlendMode::SrcOut:
      return "SrcOut";
    case BlendMode::DstOut:
      return "DstOut";
    case BlendMode::SrcATop:
      return "SrcATop";
    case BlendMode::DstATop:
      return "DstATop";
    case BlendMode::Xor:
      return "Xor";
    case BlendMode::PlusLighter:
      return "PlusLighter";
    case BlendMode::Modulate:
      return "Modulate";
    case BlendMode::Screen:
      return "Screen";
    case BlendMode::Overlay:
      return "Overlay";
    case BlendMode::Darken:
      return "Darken";
    case BlendMode::Lighten:
      return "Lighten";
    case BlendMode::ColorDodge:
      return "ColorDodge";
    case BlendMode::ColorBurn:
      return "ColorBurn";
    case BlendMode::HardLight:
      return "HardLight";
    case BlendMode::SoftLight:
      return "SoftLight";
    case BlendMode::Difference:
      return "Difference";
    case BlendMode::Exclusion:
      return "Exclusion";
    case BlendMode::Multiply:
      return "Multiply";
    case BlendMode::Hue:
      return "Hue";
    case BlendMode::Saturation:
      return "Saturation";
    case BlendMode::Color:
      return "Color";
    case BlendMode::Luminosity:
      return "Luminosity";
    case BlendMode::PlusDarker:
      return "PlusDarker";
    default:
      return "Unknown";
  }
}

std::string LayerTreeDumper::MaskTypeToString(LayerMaskType type) {
  switch (type) {
    case LayerMaskType::Alpha:
      return "Alpha";
    case LayerMaskType::Contour:
      return "Contour";
    case LayerMaskType::Luminance:
      return "Luminance";
    default:
      return "Unknown";
  }
}

std::string LayerTreeDumper::MatrixToJson(const Matrix& matrix) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << "[" << matrix.getScaleX() << ", " << matrix.getSkewX() << ", " << matrix.getTranslateX()
     << ", " << matrix.getSkewY() << ", " << matrix.getScaleY() << ", " << matrix.getTranslateY()
     << "]";
  return os.str();
}

std::string LayerTreeDumper::Matrix3DToJson(const Matrix3D& matrix) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << "[";
  for (int row = 0; row < 4; row++) {
    if (row > 0) {
      os << ", ";
    }
    os << "[";
    for (int col = 0; col < 4; col++) {
      if (col > 0) {
        os << ", ";
      }
      os << matrix.getRowColumn(row, col);
    }
    os << "]";
  }
  os << "]";
  return os.str();
}

std::string LayerTreeDumper::RectToJson(const Rect& rect) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << "{\"left\": " << rect.left << ", \"top\": " << rect.top << ", \"right\": " << rect.right
     << ", \"bottom\": " << rect.bottom << "}";
  return os.str();
}

std::string LayerTreeDumper::PointToJson(const Point& point) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << "{\"x\": " << point.x << ", \"y\": " << point.y << "}";
  return os.str();
}

std::string LayerTreeDumper::ColorToJson(const Color& color) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << "{\"red\": " << color.red << ", \"green\": " << color.green << ", \"blue\": " << color.blue
     << ", \"alpha\": " << color.alpha << "}";
  return os.str();
}

std::string LayerTreeDumper::EscapeString(const std::string& str) {
  std::ostringstream os;
  for (char c : str) {
    switch (c) {
      case '\"':
        os << "\\\"";
        break;
      case '\\':
        os << "\\\\";
        break;
      case '\b':
        os << "\\b";
        break;
      case '\f':
        os << "\\f";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        if ('\x00' <= c && c <= '\x1f') {
          os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          os << c;
        }
    }
  }
  return os.str();
}

std::string LayerTreeDumper::GetIndent(int level) {
  return std::string(static_cast<size_t>(level) * 2, ' ');
}

// ========================= Shader serialization =========================

void LayerTreeDumper::DumpShader(const std::shared_ptr<Shader>& shader, std::ostream& os,
                                 const std::string& ind, int indent) {
  if (!shader) {
    os << ind << "null";
    return;
  }
  auto shaderType = Types::Get(shader.get());
  auto ind1 = GetIndent(indent + 1);
  os << ind << "{\n";
  if (shaderType == Types::ShaderType::Color) {
    auto color = Color::Transparent();
    shader->asColor(&color);
    os << ind1 << "\"shaderType\": \"Color\",\n";
    os << ind1 << "\"color\": " << ColorToJson(color) << "\n";
  } else if (shaderType == Types::ShaderType::Gradient) {
    const auto* gradShader = static_cast<const GradientShader*>(shader.get());
    auto info = GradientInfo();
    auto gradType = gradShader->asGradient(&info);
    os << ind1 << "\"shaderType\": \"Gradient\",\n";
    os << ind1 << "\"gradientType\": \"" << GradientTypeStr(gradType) << "\",\n";
    // colors
    os << ind1 << "\"colors\": [";
    for (size_t i = 0; i < info.colors.size(); ++i) {
      if (i > 0) os << ", ";
      os << ColorToJson(info.colors[i]);
    }
    os << "],\n";
    // positions
    os << ind1 << "\"positions\": [";
    for (size_t i = 0; i < info.positions.size(); ++i) {
      if (i > 0) os << ", ";
      os << info.positions[i];
    }
    os << "],\n";
    // points
    os << ind1 << "\"points\": [" << PointToJson(info.points[0]) << ", "
       << PointToJson(info.points[1]) << "],\n";
    // radiuses
    os << ind1 << "\"radiuses\": [" << info.radiuses[0] << ", " << info.radiuses[1] << "]\n";
  } else {
    os << ind1 << "\"shaderType\": \"Unsupported\"\n";
  }
  os << ind << "}";
}

// ========================= ColorSource serialization =========================

void LayerTreeDumper::DumpColorSource(const std::shared_ptr<ColorSource>& source, std::ostream& os,
                                      int indent) {
  if (!source) {
    os << "null";
    return;
  }
  auto ind = GetIndent(indent);
  auto ind1 = GetIndent(indent + 1);
  auto sourceType = Types::Get(source.get());
  os << "{\n";
  if (sourceType == Types::ColorSourceType::SolidColor) {
    const auto* solid = static_cast<const SolidColor*>(source.get());
    os << ind1 << "\"colorSourceType\": \"SolidColor\",\n";
    os << ind1 << "\"color\": " << ColorToJson(solid->color()) << "\n";
  } else if (sourceType == Types::ColorSourceType::Gradient) {
    const auto* gradient = static_cast<const Gradient*>(source.get());
    os << ind1 << "\"colorSourceType\": \"Gradient\",\n";
    os << ind1 << "\"gradientType\": \"" << GradientTypeStr(gradient->type()) << "\",\n";
    // colors
    os << ind1 << "\"colors\": [";
    for (size_t i = 0; i < gradient->colors().size(); ++i) {
      if (i > 0) os << ", ";
      os << ColorToJson(gradient->colors()[i]);
    }
    os << "],\n";
    // positions
    os << ind1 << "\"positions\": [";
    for (size_t i = 0; i < gradient->positions().size(); ++i) {
      if (i > 0) os << ", ";
      os << gradient->positions()[i];
    }
    os << "],\n";
    // matrix
    os << ind1 << "\"matrix\": " << MatrixToJson(gradient->matrix()) << ",\n";
    // gradient-specific params
    if (gradient->type() == GradientType::Linear) {
      const auto* linear = static_cast<const LinearGradient*>(gradient);
      os << ind1 << "\"startPoint\": " << PointToJson(linear->startPoint()) << ",\n";
      os << ind1 << "\"endPoint\": " << PointToJson(linear->endPoint()) << "\n";
    } else if (gradient->type() == GradientType::Radial) {
      const auto* radial = static_cast<const RadialGradient*>(gradient);
      os << ind1 << "\"center\": " << PointToJson(radial->center()) << ",\n";
      os << ind1 << "\"radius\": " << std::fixed << std::setprecision(6) << radial->radius()
         << "\n";
    } else if (gradient->type() == GradientType::Conic) {
      const auto* conic = static_cast<const ConicGradient*>(gradient);
      os << ind1 << "\"center\": " << PointToJson(conic->center()) << ",\n";
      os << ind1 << "\"startAngle\": " << std::fixed << std::setprecision(6) << conic->startAngle()
         << ",\n";
      os << ind1 << "\"endAngle\": " << conic->endAngle() << "\n";
    } else if (gradient->type() == GradientType::Diamond) {
      const auto* diamond = static_cast<const DiamondGradient*>(gradient);
      os << ind1 << "\"center\": " << PointToJson(diamond->center()) << ",\n";
      os << ind1 << "\"radius\": " << std::fixed << std::setprecision(6) << diamond->radius()
         << "\n";
    }
  } else {
    os << ind1 << "\"colorSourceType\": \"Unsupported\"\n";
  }
  os << ind << "}";
}

// ========================= ShapeStyle serialization =========================

void LayerTreeDumper::DumpShapeStyles(const std::vector<std::shared_ptr<ShapeStyle>>& styles,
                                      std::ostream& os, const std::string& ind, int indent,
                                      const char* fieldName) {
  auto ind2 = GetIndent(indent + 1);
  os << ind << "\"" << fieldName << "\": [";
  if (styles.empty()) {
    os << "],\n";
    return;
  }
  os << "\n";
  for (size_t i = 0; i < styles.size(); ++i) {
    const auto& style = styles[i];
    os << ind2 << "{\n";
    auto ind3 = GetIndent(indent + 2);
    os << ind3 << "\"color\": " << ColorToJson(style->color()) << ",\n";
    os << ind3 << "\"blendMode\": \"" << BlendModeToString(style->blendMode()) << "\",\n";
    os << ind3 << "\"shader\": ";
    DumpShader(style->shader(), os, "", indent + 2);
    os << "\n";
    os << ind2 << "}" << (i + 1 < styles.size() ? "," : "") << "\n";
  }
  os << ind << "],\n";
}

// ========================= LayerFilter serialization =========================

void LayerTreeDumper::DumpFilters(const std::vector<std::shared_ptr<LayerFilter>>& filters,
                                  std::ostream& os, const std::string& ind, int indent) {
  auto ind2 = GetIndent(indent + 1);
  os << ind << "\"filters\": [";
  if (filters.empty()) {
    os << "],\n";
    return;
  }
  os << "\n";
  for (size_t i = 0; i < filters.size(); ++i) {
    const auto& filter = filters[i];
    auto filterType = Types::Get(filter.get());
    auto ind3 = GetIndent(indent + 2);
    os << ind2 << "{\n";
    if (filterType == Types::LayerFilterType::DropShadowFilter) {
      const auto* f = static_cast<const DropShadowFilter*>(filter.get());
      os << ind3 << "\"filterType\": \"DropShadow\",\n";
      os << ind3 << "\"offsetX\": " << std::fixed << std::setprecision(6) << f->offsetX() << ",\n";
      os << ind3 << "\"offsetY\": " << f->offsetY() << ",\n";
      os << ind3 << "\"blurrinessX\": " << f->blurrinessX() << ",\n";
      os << ind3 << "\"blurrinessY\": " << f->blurrinessY() << ",\n";
      os << ind3 << "\"color\": " << ColorToJson(f->color()) << ",\n";
      os << ind3 << "\"dropsShadowOnly\": " << (f->dropsShadowOnly() ? "true" : "false") << "\n";
    } else if (filterType == Types::LayerFilterType::InnerShadowFilter) {
      const auto* f = static_cast<const InnerShadowFilter*>(filter.get());
      os << ind3 << "\"filterType\": \"InnerShadow\",\n";
      os << ind3 << "\"offsetX\": " << std::fixed << std::setprecision(6) << f->offsetX() << ",\n";
      os << ind3 << "\"offsetY\": " << f->offsetY() << ",\n";
      os << ind3 << "\"blurrinessX\": " << f->blurrinessX() << ",\n";
      os << ind3 << "\"blurrinessY\": " << f->blurrinessY() << ",\n";
      os << ind3 << "\"color\": " << ColorToJson(f->color()) << ",\n";
      os << ind3 << "\"innerShadowOnly\": " << (f->innerShadowOnly() ? "true" : "false") << "\n";
    } else if (filterType == Types::LayerFilterType::BlurFilter) {
      const auto* f = static_cast<const BlurFilter*>(filter.get());
      os << ind3 << "\"filterType\": \"Blur\",\n";
      os << ind3 << "\"blurrinessX\": " << std::fixed << std::setprecision(6) << f->blurrinessX()
         << ",\n";
      os << ind3 << "\"blurrinessY\": " << f->blurrinessY() << ",\n";
      os << ind3 << "\"tileMode\": \"" << TileModeStr(f->tileMode()) << "\"\n";
    } else if (filterType == Types::LayerFilterType::BlendFilter) {
      const auto* f = static_cast<const BlendFilter*>(filter.get());
      os << ind3 << "\"filterType\": \"Blend\",\n";
      os << ind3 << "\"color\": " << ColorToJson(f->color()) << ",\n";
      os << ind3 << "\"blendMode\": \"" << BlendModeToString(f->blendMode()) << "\"\n";
    } else if (filterType == Types::LayerFilterType::ColorMatrixFilter) {
      const auto* f = static_cast<const ColorMatrixFilter*>(filter.get());
      os << ind3 << "\"filterType\": \"ColorMatrix\",\n";
      os << ind3 << "\"matrix\": [";
      auto matrix = f->matrix();
      for (size_t j = 0; j < 20; ++j) {
        if (j > 0) os << ", ";
        os << matrix[j];
      }
      os << "]\n";
    } else {
      os << ind3 << "\"filterType\": \"Unknown\"\n";
    }
    os << ind2 << "}" << (i + 1 < filters.size() ? "," : "") << "\n";
  }
  os << ind << "],\n";
}

// ========================= LayerStyle serialization =========================

void LayerTreeDumper::DumpLayerStyles(const std::vector<std::shared_ptr<LayerStyle>>& styles,
                                      std::ostream& os, const std::string& ind, int indent) {
  auto ind2 = GetIndent(indent + 1);
  os << ind << "\"layerStyles\": [";
  if (styles.empty()) {
    os << "],\n";
    return;
  }
  os << "\n";
  for (size_t i = 0; i < styles.size(); ++i) {
    const auto& style = styles[i];
    auto ind3 = GetIndent(indent + 2);
    os << ind2 << "{\n";
    auto styleType = style->Type();
    os << ind3 << "\"blendMode\": \"" << BlendModeToString(style->blendMode()) << "\",\n";
    os << ind3 << "\"excludeChildEffects\": " << (style->excludeChildEffects() ? "true" : "false")
       << ",\n";
    if (styleType == LayerStyleType::DropShadow) {
      const auto* s = static_cast<const DropShadowStyle*>(style.get());
      os << ind3 << "\"styleType\": \"DropShadow\",\n";
      os << ind3 << "\"offsetX\": " << std::fixed << std::setprecision(6) << s->offsetX() << ",\n";
      os << ind3 << "\"offsetY\": " << s->offsetY() << ",\n";
      os << ind3 << "\"blurrinessX\": " << s->blurrinessX() << ",\n";
      os << ind3 << "\"blurrinessY\": " << s->blurrinessY() << ",\n";
      os << ind3 << "\"color\": " << ColorToJson(s->color()) << ",\n";
      os << ind3 << "\"showBehindLayer\": " << (s->showBehindLayer() ? "true" : "false") << "\n";
    } else if (styleType == LayerStyleType::InnerShadow) {
      const auto* s = static_cast<const InnerShadowStyle*>(style.get());
      os << ind3 << "\"styleType\": \"InnerShadow\",\n";
      os << ind3 << "\"offsetX\": " << std::fixed << std::setprecision(6) << s->offsetX() << ",\n";
      os << ind3 << "\"offsetY\": " << s->offsetY() << ",\n";
      os << ind3 << "\"blurrinessX\": " << s->blurrinessX() << ",\n";
      os << ind3 << "\"blurrinessY\": " << s->blurrinessY() << ",\n";
      os << ind3 << "\"color\": " << ColorToJson(s->color()) << "\n";
    } else if (styleType == LayerStyleType::BackgroundBlur) {
      const auto* s = static_cast<const BackgroundBlurStyle*>(style.get());
      os << ind3 << "\"styleType\": \"BackgroundBlur\",\n";
      os << ind3 << "\"blurrinessX\": " << std::fixed << std::setprecision(6) << s->blurrinessX()
         << ",\n";
      os << ind3 << "\"blurrinessY\": " << s->blurrinessY() << ",\n";
      os << ind3 << "\"tileMode\": \"" << TileModeStr(s->tileMode()) << "\"\n";
    } else {
      os << ind3 << "\"styleType\": \"Unknown\"\n";
    }
    os << ind2 << "}" << (i + 1 < styles.size() ? "," : "") << "\n";
  }
  os << ind << "],\n";
}

// ========================= Layer subclass property dumpers =========================

void LayerTreeDumper::DumpSolidLayerProps(const Layer* layer, std::ostream& os,
                                          const std::string& ind) {
  const auto* solid = static_cast<const SolidLayer*>(layer);
  os << std::fixed << std::setprecision(6);
  os << ind << "\"solidWidth\": " << solid->width() << ",\n";
  os << ind << "\"solidHeight\": " << solid->height() << ",\n";
  os << ind << "\"solidRadiusX\": " << solid->radiusX() << ",\n";
  os << ind << "\"solidRadiusY\": " << solid->radiusY() << ",\n";
  os << ind << "\"solidColor\": " << ColorToJson(solid->color()) << ",\n";
}

void LayerTreeDumper::DumpShapeLayerProps(const Layer* layer, std::ostream& os,
                                          const std::string& ind) {
  const auto* shape = static_cast<const ShapeLayer*>(layer);
  auto path = shape->path();
  auto pathStr = SVGPathParser::ToSVGString(path);
  os << std::fixed << std::setprecision(6);
  os << ind << "\"shapePath\": \"" << EscapeString(pathStr) << "\",\n";
  os << ind << "\"lineWidth\": " << shape->lineWidth() << ",\n";
  os << ind << "\"lineCap\": " << static_cast<int>(shape->lineCap()) << ",\n";
  os << ind << "\"lineJoin\": " << static_cast<int>(shape->lineJoin()) << ",\n";
  os << ind << "\"miterLimit\": " << shape->miterLimit() << ",\n";
  os << ind << "\"lineDashPhase\": " << shape->lineDashPhase() << ",\n";
  os << ind << "\"lineDashAdaptive\": " << (shape->lineDashAdaptive() ? "true" : "false") << ",\n";
  os << ind << "\"strokeAlign\": \"" << StrokeAlignStr(shape->strokeAlign()) << "\",\n";
  os << ind << "\"strokeOnTop\": " << (shape->strokeOnTop() ? "true" : "false") << ",\n";
  const auto& dashPattern = shape->lineDashPattern();
  os << ind << "\"lineDashPattern\": [";
  for (size_t i = 0; i < dashPattern.size(); ++i) {
    if (i > 0) os << ", ";
    os << dashPattern[i];
  }
  os << "],\n";
  auto indentLevel = static_cast<int>(ind.size()) / 2;
  DumpShapeStyles(shape->fillStyles(), os, ind, indentLevel, "fillStyles");
  DumpShapeStyles(shape->strokeStyles(), os, ind, indentLevel, "strokeStyles");
}

void LayerTreeDumper::DumpTextLayerProps(const Layer* layer, std::ostream& os,
                                         const std::string& ind) {
  const auto* text = static_cast<const TextLayer*>(layer);
  os << std::fixed << std::setprecision(6);
  os << ind << "\"text\": \"" << EscapeString(text->text()) << "\",\n";
  os << ind << "\"textColor\": " << ColorToJson(text->textColor()) << ",\n";
  os << ind << "\"textWidth\": " << text->width() << ",\n";
  os << ind << "\"textHeight\": " << text->height() << ",\n";
  os << ind << "\"textAlign\": " << static_cast<int>(text->textAlign()) << ",\n";
  os << ind << "\"autoWrap\": " << (text->autoWrap() ? "true" : "false") << ",\n";
  // Font serialization: size and typeface family name
  const auto& font = text->font();
  os << ind << "\"fontSize\": " << font.getSize() << ",\n";
  auto typeface = font.getTypeface();
  if (typeface) {
    os << ind << "\"fontFamily\": \"" << EscapeString(typeface->fontFamily()) << "\",\n";
    os << ind << "\"fontStyle\": \"" << EscapeString(typeface->fontStyle()) << "\",\n";
  }
}

void LayerTreeDumper::DumpImageLayerProps(const Layer*, std::ostream& os, const std::string& ind) {
  // Image resources cannot be meaningfully serialized to JSON
  os << ind << "\"imageNote\": \"Image resources are not serialized\",\n";
}

// ========================= VectorElement serialization =========================

void LayerTreeDumper::DumpVectorElement(const VectorElement* element, std::ostream& os, int indent,
                                        bool isLast) {
  if (!element) return;
  auto ind = GetIndent(indent);
  auto ind1 = GetIndent(indent + 1);
  os << ind << "{\n";
  os << ind1 << "\"enabled\": " << (element->enabled() ? "true" : "false") << ",\n";

  if (auto rect = dynamic_cast<const Rectangle*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"Rectangle\",\n";
    os << ind1 << "\"position\": " << PointToJson(GetPosition(rect)) << ",\n";
    os << ind1 << "\"size\": {\"width\": " << rect->size().width
       << ", \"height\": " << rect->size().height << "},\n";
    os << ind1 << "\"roundness\": " << rect->roundness() << ",\n";
    os << ind1 << "\"reversed\": " << (rect->reversed() ? "true" : "false") << "\n";
  } else if (auto ellipse = dynamic_cast<const Ellipse*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"Ellipse\",\n";
    os << ind1 << "\"position\": " << PointToJson(GetPosition(ellipse)) << ",\n";
    os << ind1 << "\"size\": {\"width\": " << ellipse->size().width
       << ", \"height\": " << ellipse->size().height << "},\n";
    os << ind1 << "\"reversed\": " << (ellipse->reversed() ? "true" : "false") << "\n";
  } else if (auto polystar = dynamic_cast<const Polystar*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"Polystar\",\n";
    os << ind1 << "\"position\": " << PointToJson(polystar->position()) << ",\n";
    os << ind1 << "\"polystarType\": \"" << PolystarTypeStr(polystar->polystarType()) << "\",\n";
    os << ind1 << "\"pointCount\": " << polystar->pointCount() << ",\n";
    os << ind1 << "\"rotation\": " << polystar->rotation() << ",\n";
    os << ind1 << "\"outerRadius\": " << polystar->outerRadius() << ",\n";
    os << ind1 << "\"outerRoundness\": " << polystar->outerRoundness() << ",\n";
    os << ind1 << "\"innerRadius\": " << polystar->innerRadius() << ",\n";
    os << ind1 << "\"innerRoundness\": " << polystar->innerRoundness() << ",\n";
    os << ind1 << "\"reversed\": " << (polystar->reversed() ? "true" : "false") << "\n";
  } else if (auto shapePath = dynamic_cast<const ShapePath*>(element)) {
    auto pathStr = SVGPathParser::ToSVGString(shapePath->path());
    os << ind1 << "\"elementType\": \"ShapePath\",\n";
    os << ind1 << "\"path\": \"" << EscapeString(pathStr) << "\",\n";
    os << ind1 << "\"reversed\": " << (shapePath->reversed() ? "true" : "false") << "\n";
  } else if (auto fill = dynamic_cast<const FillStyle*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"FillStyle\",\n";
    os << ind1 << "\"alpha\": " << fill->alpha() << ",\n";
    os << ind1 << "\"blendMode\": \"" << BlendModeToString(fill->blendMode()) << "\",\n";
    os << ind1 << "\"fillRule\": \"" << FillRuleStr(fill->fillRule()) << "\",\n";
    os << ind1 << "\"placement\": \"" << LayerPlacementStr(fill->placement()) << "\",\n";
    os << ind1 << "\"colorSource\": ";
    DumpColorSource(fill->colorSource(), os, indent + 1);
    os << "\n";
  } else if (auto stroke = dynamic_cast<const StrokeStyle*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"StrokeStyle\",\n";
    os << ind1 << "\"alpha\": " << stroke->alpha() << ",\n";
    os << ind1 << "\"blendMode\": \"" << BlendModeToString(stroke->blendMode()) << "\",\n";
    os << ind1 << "\"strokeWidth\": " << stroke->strokeWidth() << ",\n";
    os << ind1 << "\"lineCap\": " << static_cast<int>(stroke->lineCap()) << ",\n";
    os << ind1 << "\"lineJoin\": " << static_cast<int>(stroke->lineJoin()) << ",\n";
    os << ind1 << "\"miterLimit\": " << stroke->miterLimit() << ",\n";
    os << ind1 << "\"dashAdaptive\": " << (stroke->dashAdaptive() ? "true" : "false") << ",\n";
    os << ind1 << "\"dashOffset\": " << stroke->dashOffset() << ",\n";
    os << ind1 << "\"strokeAlign\": \"" << StrokeAlignStr(stroke->strokeAlign()) << "\",\n";
    os << ind1 << "\"placement\": \"" << LayerPlacementStr(stroke->placement()) << "\",\n";
    const auto& dashes = stroke->dashes();
    os << ind1 << "\"dashes\": [";
    for (size_t j = 0; j < dashes.size(); ++j) {
      if (j > 0) os << ", ";
      os << dashes[j];
    }
    os << "],\n";
    os << ind1 << "\"colorSource\": ";
    DumpColorSource(stroke->colorSource(), os, indent + 1);
    os << "\n";
  } else if (auto trimPath = dynamic_cast<const TrimPath*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"TrimPath\",\n";
    os << ind1 << "\"start\": " << trimPath->start() << ",\n";
    os << ind1 << "\"end\": " << trimPath->end() << ",\n";
    os << ind1 << "\"offset\": " << trimPath->offset() << ",\n";
    os << ind1 << "\"trimType\": \"" << TrimPathTypeStr(trimPath->trimType()) << "\"\n";
  } else if (auto roundCorner = dynamic_cast<const RoundCorner*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"RoundCorner\",\n";
    os << ind1 << "\"radius\": " << roundCorner->radius() << "\n";
  } else if (auto mergePath = dynamic_cast<const MergePath*>(element)) {
    os << ind1 << "\"elementType\": \"MergePath\",\n";
    os << ind1 << "\"mode\": \"" << MergePathOpStr(mergePath->mode()) << "\"\n";
  } else if (auto repeater = dynamic_cast<const Repeater*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"Repeater\",\n";
    os << ind1 << "\"copies\": " << repeater->copies() << ",\n";
    os << ind1 << "\"offset\": " << repeater->offset() << ",\n";
    os << ind1 << "\"order\": \"" << RepeaterOrderStr(repeater->order()) << "\",\n";
    os << ind1 << "\"anchor\": " << PointToJson(repeater->anchor()) << ",\n";
    os << ind1 << "\"position\": " << PointToJson(repeater->position()) << ",\n";
    os << ind1 << "\"rotation\": " << repeater->rotation() << ",\n";
    os << ind1 << "\"scale\": " << PointToJson(repeater->scale()) << ",\n";
    os << ind1 << "\"startAlpha\": " << repeater->startAlpha() << ",\n";
    os << ind1 << "\"endAlpha\": " << repeater->endAlpha() << "\n";
  } else if (auto group = dynamic_cast<const VectorGroup*>(element)) {
    os << std::fixed << std::setprecision(6);
    os << ind1 << "\"elementType\": \"VectorGroup\",\n";
    os << ind1 << "\"anchor\": " << PointToJson(group->anchor()) << ",\n";
    os << ind1 << "\"position\": " << PointToJson(group->position()) << ",\n";
    os << ind1 << "\"scale\": " << PointToJson(group->scale()) << ",\n";
    os << ind1 << "\"rotation\": " << group->rotation() << ",\n";
    os << ind1 << "\"alpha\": " << group->alpha() << ",\n";
    os << ind1 << "\"skew\": " << group->skew() << ",\n";
    os << ind1 << "\"skewAxis\": " << group->skewAxis() << ",\n";
    os << ind1 << "\"elements\": ";
    DumpVectorElements(group->elements(), os, indent + 1);
    os << "\n";
  } else {
    os << ind1 << "\"elementType\": \"Unknown\"\n";
  }
  os << ind << "}" << (isLast ? "" : ",") << "\n";
}

void LayerTreeDumper::DumpVectorElements(
    const std::vector<std::shared_ptr<VectorElement>>& elements, std::ostream& os, int indent) {
  os << "[\n";
  for (size_t i = 0; i < elements.size(); ++i) {
    DumpVectorElement(elements[i].get(), os, indent + 1, i == elements.size() - 1);
  }
  os << GetIndent(indent) << "]";
}

void LayerTreeDumper::DumpVectorLayerProps(const Layer* layer, std::ostream& os,
                                           const std::string& ind, int indent) {
  const auto* vector = static_cast<const VectorLayer*>(layer);
  os << ind << "\"contents\": ";
  DumpVectorElements(vector->contents(), os, indent);
  os << ",\n";
}

// ========================= Main layer dump =========================

void LayerTreeDumper::DumpLayerToStream(const Layer* layer, std::ostream& os, int indent,
                                        bool isLast) {
  if (layer == nullptr) {
    return;
  }

  auto ind = GetIndent(indent);
  auto ind1 = GetIndent(indent + 1);

  os << ind << "{\n";

  // Basic properties
  os << ind1 << "\"type\": \"" << LayerTypeToString(layer->type()) << "\",\n";
  os << ind1 << "\"name\": \"" << EscapeString(layer->name()) << "\",\n";
  os << ind1 << "\"alpha\": " << std::fixed << std::setprecision(6) << layer->alpha() << ",\n";
  os << ind1 << "\"visible\": " << (layer->visible() ? "true" : "false") << ",\n";
  os << ind1 << "\"blendMode\": \"" << BlendModeToString(layer->blendMode()) << "\",\n";

  // Transform
  os << ind1 << "\"matrix\": " << MatrixToJson(layer->matrix()) << ",\n";
  os << ind1 << "\"matrix3D\": " << Matrix3DToJson(layer->matrix3D()) << ",\n";
  os << ind1 << "\"position\": " << PointToJson(layer->position()) << ",\n";

  // 3D properties
  os << ind1 << "\"preserve3D\": " << (layer->preserve3D() ? "true" : "false") << ",\n";

  // Rendering properties
  os << ind1
     << "\"allowsEdgeAntialiasing\": " << (layer->allowsEdgeAntialiasing() ? "true" : "false")
     << ",\n";
  os << ind1 << "\"allowsGroupOpacity\": " << (layer->allowsGroupOpacity() ? "true" : "false")
     << ",\n";
  os << ind1 << "\"passThroughBackground\": " << (layer->passThroughBackground() ? "true" : "false")
     << ",\n";

  // Scroll rect
  auto scrollRect = layer->scrollRect();
  os << ind1 << "\"scrollRect\": " << RectToJson(scrollRect) << ",\n";

  // Mask
  os << ind1 << "\"maskType\": \"" << MaskTypeToString(layer->maskType()) << "\",\n";
  os << ind1 << "\"hasMask\": " << (layer->mask() != nullptr ? "true" : "false") << ",\n";

  // Filters (full serialization)
  DumpFilters(layer->filters(), os, ind1, indent + 1);

  // Layer styles (full serialization)
  DumpLayerStyles(layer->layerStyles(), os, ind1, indent + 1);

  // Layer subclass-specific properties
  switch (layer->type()) {
    case LayerType::Solid:
      DumpSolidLayerProps(layer, os, ind1);
      break;
    case LayerType::Shape:
      DumpShapeLayerProps(layer, os, ind1);
      break;
    case LayerType::Text:
      DumpTextLayerProps(layer, os, ind1);
      break;
    case LayerType::Image:
      DumpImageLayerProps(layer, os, ind1);
      break;
    case LayerType::Vector:
      DumpVectorLayerProps(layer, os, ind1, indent + 1);
      break;
    default:
      break;
  }

  // Bounds
  auto bounds = const_cast<Layer*>(layer)->getBounds();
  os << ind1 << "\"bounds\": " << RectToJson(bounds) << ",\n";

  // Children
  const auto& children = layer->children();
  os << ind1 << "\"childrenCount\": " << children.size() << ",\n";
  os << ind1 << "\"children\": [\n";

  for (size_t i = 0; i < children.size(); ++i) {
    DumpLayerToStream(children[i].get(), os, indent + 2, i == children.size() - 1);
  }

  os << ind1 << "]\n";
  os << ind << "}" << (isLast ? "" : ",") << "\n";
}

void LayerTreeDumper::DumpTree(const Layer* root, const std::string& outputDir) {
  if (root == nullptr || outputDir.empty()) {
    return;
  }

  auto filePath = outputDir + "/layer_tree.json";
  std::ofstream file(filePath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    LOGE("Failed to open file for writing: %s", filePath.c_str());
    return;
  }

  // Disable buffering synchronization for better I/O error detection
  file.exceptions(std::ofstream::failbit | std::ofstream::badbit);

  try {
    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"root\": \n";
    DumpLayerToStream(root, file, 1, true);
    file << "}\n";

    // Ensure all data is flushed to disk
    file.flush();

    if (!file.good()) {
      LOGE("Error occurred while writing to file: %s", filePath.c_str());
      file.close();
      return;
    }

    file.close();
    LOGI("Layer tree dumped to: %s", filePath.c_str());
  } catch (const std::ios_base::failure& e) {
    LOGE("I/O error while writing layer tree to %s: %s", filePath.c_str(), e.what());
    file.close();
  }
}

}  // namespace tgfx
