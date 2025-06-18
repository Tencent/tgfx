/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#ifdef TGFX_USE_INSPECTOR
#include "ColorFilterSerialization.h"
#include "ColorSerialization.h"
#include "FontMetricsSerialization.h"
#include "FontSerialization.h"
#include "ImageFilterSerialization.h"
#include "ImageSerialization.h"
#include "LayerFilterSerialization.h"
#include "LayerSerialization.h"
#include "LayerStyleSerialization.h"
#include "MatrixSerialization.h"
#include "PathSerialization.h"
#include "PointSerialization.h"
#include "RectSerialization.h"
#include "RuntimeEffectSerialization.h"
#include "SamplingOptionsSerialization.h"
#include "SerializationUtils.h"
#include "ShaderSerialization.h"
#include "ShapeSerialization.h"
#include "ShapeStyleSerialization.h"
#include "TypeFaceSerialization.h"
#include "TextBlobSerialization.h"
#include "core/images/FilterImage.h"
#include "glyphRunSerialization.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

constexpr int padding = 20;
  static int insertionCounter = 0;
std::string SerializeUtils::LayerTypeToString(LayerType type) {
  static std::unordered_map<LayerType, const char*> m = {
      {LayerType::Layer, "Layer"},      {LayerType::Image, "ImageLayer"},
      {LayerType::Shape, "ShapeLayer"}, {LayerType::Gradient, "GradientLayer"},
      {LayerType::Text, "TextLayer"},   {LayerType::Solid, "SolidLayer"}};
  return m[type];
}

std::string SerializeUtils::BlendModeToString(BlendMode mode) {
  static std::unordered_map<BlendMode, const char*> m = {{BlendMode::Clear, "Clear"},
                                                         {BlendMode::Src, "Src"},
                                                         {BlendMode::Dst, "Dst"},
                                                         {BlendMode::SrcOver, "SrcOver"},
                                                         {BlendMode::DstOver, "DstOver"},
                                                         {BlendMode::SrcIn, "SrcIn"},
                                                         {BlendMode::DstIn, "DstIn"},
                                                         {BlendMode::SrcOut, "SrcOut"},
                                                         {BlendMode::DstOut, "DstOut"},
                                                         {BlendMode::SrcATop, "SrcATop"},
                                                         {BlendMode::DstATop, "DstATop"},
                                                         {BlendMode::Xor, "Xor"},
                                                         {BlendMode::PlusLighter, "PlusLighter"},
                                                         {BlendMode::Modulate, "Modulate"},
                                                         {BlendMode::Screen, "Screen"},
                                                         {BlendMode::Overlay, "Overlay"},
                                                         {BlendMode::Darken, "Darken"},
                                                         {BlendMode::Lighten, "Lighten"},
                                                         {BlendMode::ColorDodge, "ColorDodge"},
                                                         {BlendMode::ColorBurn, "ColorBurn"},
                                                         {BlendMode::HardLight, "HardLight"},
                                                         {BlendMode::SoftLight, "SoftLight"},
                                                         {BlendMode::Difference, "Difference"},
                                                         {BlendMode::Exclusion, "Exclusion"},
                                                         {BlendMode::Multiply, "Multiply"},
                                                         {BlendMode::Hue, "Hue"},
                                                         {BlendMode::Saturation, "Saturation"},
                                                         {BlendMode::Color, "Color"},
                                                         {BlendMode::Luminosity, "Luminosity"},
                                                         {BlendMode::PlusDarker, "PlusDarker"}};
  return m[mode];
}

std::string SerializeUtils::StrokeAlignToString(StrokeAlign align) {
  static std::unordered_map<StrokeAlign, const char*> m = {{StrokeAlign::Center, "Center"},
                                                           {StrokeAlign::Inside, "Inside"},
                                                           {StrokeAlign::Outside, "Outside"}};
  return m[align];
}

std::string SerializeUtils::TextAlignToString(TextAlign align) {
  static std::unordered_map<TextAlign, const char*> m = {{TextAlign::Left, "Left"},
                                                         {TextAlign::Right, "Right"},
                                                         {TextAlign::Center, "Center"},
                                                         {TextAlign::Justify, "Justify"}};
  return m[align];
}

std::string SerializeUtils::TileModeToString(TileMode tileMode) {
  static std::unordered_map<TileMode, const char*> m = {{TileMode::Clamp, "Clamp"},
                                                        {TileMode::Repeat, "Repeat"},
                                                        {TileMode::Mirror, "Mirror"},
                                                        {TileMode::Decal, "Decal"}};
  return m[tileMode];
}

std::string SerializeUtils::ImageTypeToString(Types::ImageType type) {
  static std::unordered_map<Types::ImageType, const char*> m = {
      {Types::ImageType::Buffer, "Buffer"},         {Types::ImageType::Codec, "Codec"},
      {Types::ImageType::Decoded, "Decoded"},       {Types::ImageType::Filter, "Filter"},
      {Types::ImageType::Generator, "Generator"},   {Types::ImageType::Mipmap, "Mipmap"},
      {Types::ImageType::Orient, "Orient"},         {Types::ImageType::Picture, "Picture"},
      {Types::ImageType::Rasterized, "Rasterized"}, {Types::ImageType::RGBAAA, "RGBAA"},
      {Types::ImageType::Texture, "Texture"},       {Types::ImageType::Subset, "Subset"}};
  return m[type];
}

std::string SerializeUtils::FilterModeToString(FilterMode mode) {
  static std::unordered_map<FilterMode, const char*> m = {{FilterMode::Linear, "Linear"},
                                                          {FilterMode::Nearest, "Nearest"}};
  return m[mode];
}

std::string SerializeUtils::MipmapModeToString(MipmapMode mode) {
  static std::unordered_map<MipmapMode, const char*> m = {{MipmapMode::Linear, "Linear"},
                                                          {MipmapMode::Nearest, "Nearest"},
                                                          {MipmapMode::None, "None"}};
  return m[mode];
}

std::string SerializeUtils::ShapeTypeToString(Types::ShapeType type) {
  static std::unordered_map<Types::ShapeType, const char*> m = {
      {Types::ShapeType::Append, "Append"}, {Types::ShapeType::Effect, "Effect"},
      {Types::ShapeType::Text, "Text"},     {Types::ShapeType::Inverse, "Inverse"},
      {Types::ShapeType::Matrix, "Matrix"}, {Types::ShapeType::Merge, "Merge"},
      {Types::ShapeType::Path, "Path"},     {Types::ShapeType::Provider, "Provider"},
      {Types::ShapeType::Stroke, "Stroke"}, {Types::ShapeType::Glyph, "Glyph"},
  };
  return m[type];
}

std::string SerializeUtils::ShaderTypeToString(Types::ShaderType type) {
  static std::unordered_map<Types::ShaderType, const char*> m = {
      {Types::ShaderType::Color, "Color"},   {Types::ShaderType::ColorFilter, "ColorFilter"},
      {Types::ShaderType::Image, "Image"},   {Types::ShaderType::Blend, "Blend"},
      {Types::ShaderType::Matrix, "Matrix"}, {Types::ShaderType::Gradient, "Gradient"}};
  return m[type];
}

std::string SerializeUtils::LineCapToString(LineCap lineCap) {
  static std::unordered_map<LineCap, const char*> m = {{LineCap::Butt, "Butt"},
                                                       {LineCap::Round, "Round"},
                                                       {LineCap::Square, "Square"}};
  return m[lineCap];
}

std::string SerializeUtils::LineJoinToString(LineJoin lineJoin) {
  static std::unordered_map<LineJoin, const char*> m = {{LineJoin::Miter, "Miter"},
                                                        {LineJoin::Round, "Round"},
                                                        {LineJoin::Bevel, "Bevel"}};
  return m[lineJoin];
}

std::string SerializeUtils::ImageFilterTypeToString(Types::ImageFilterType type) {
  static std::unordered_map<Types::ImageFilterType, const char*> m = {
      {Types::ImageFilterType::Blur, "Blur"},
      {Types::ImageFilterType::Color, "Color"},
      {Types::ImageFilterType::Compose, "Compose"},
      {Types::ImageFilterType::Runtime, "Runtime"},
      {Types::ImageFilterType::DropShadow, "DropShadow"},
      {Types::ImageFilterType::InnerShadow, "InnerShadow"}};
  return m[type];
}

std::string SerializeUtils::ColorFilterTypeToString(Types::ColorFilterType type) {
  static std::unordered_map<Types::ColorFilterType, const char*> m = {
      {Types::ColorFilterType::Blend, "Blend"},
      {Types::ColorFilterType::Matrix, "Matrix"},
      {Types::ColorFilterType::AlphaThreshold, "AlphaThreshold"},
      {Types::ColorFilterType::Compose, "Compose"}};
  return m[type];
}

std::string SerializeUtils::LayerFilterTypeToString(Types::LayerFilterType type) {
  static std::unordered_map<Types::LayerFilterType, const char*> m = {
      {Types::LayerFilterType::LayerFilter, "LayerFilter"},
      {Types::LayerFilterType::BlendFilter, "BlendFilter"},
      {Types::LayerFilterType::BlurFilter, "BlurFilter"},
      {Types::LayerFilterType::ColorMatrixFilter, "ColorMatrixFilter"},
      {Types::LayerFilterType::DropShadowFilter, "DropShadowFilter"},
      {Types::LayerFilterType::InnerShadowFilter, "InnerShadowFilter"}};
  return m[type];
}

std::string SerializeUtils::LayerStyleTypeToString(LayerStyleType type) {
  static std::unordered_map<LayerStyleType, const char*> m = {
      {LayerStyleType::LayerStyle, "LayerStyle"},
      {LayerStyleType::BackgroundBlur, "BackgroundBlur"},
      {LayerStyleType::DropShadow, "DropShadow"},
      {LayerStyleType::InnerShadow, "InnerShadow"}};
  return m[type];
}

std::string SerializeUtils::LayerStylePositionToString(LayerStylePosition position) {
  static std::unordered_map<LayerStylePosition, const char*> m = {
      {LayerStylePosition::Above, "Above"},
      {LayerStylePosition::Below, "Below"}};
  return m[position];
}

std::string SerializeUtils::LayerStyleExtraSourceTypeToString(LayerStyleExtraSourceType type) {
  static std::unordered_map<LayerStyleExtraSourceType, const char*> m = {
      {LayerStyleExtraSourceType::None, "None"},
      {LayerStyleExtraSourceType::Background, "Background"},
      {LayerStyleExtraSourceType::Contour, "Contour"}};
  return m[type];
}

std::string SerializeUtils::ShapeStyleTypeToString(Types::ShapeStyleType type) {
  static std::unordered_map<Types::ShapeStyleType, const char*> m = {
      {Types::ShapeStyleType::Gradient, "Gradient"},
      {Types::ShapeStyleType::ImagePattern, "ImagePattern"},
      {Types::ShapeStyleType::SolidColor, "SolidColor"}};
  return m[type];
}

std::string SerializeUtils::GradientTypeToString(GradientType type) {
  static std::unordered_map<GradientType, const char*> m = {{GradientType::Conic, "Conic"},
                                                            {GradientType::Diamond, "Diamond"},
                                                            {GradientType::Linear, "Linear"},
                                                            {GradientType::None, "None"},
                                                            {GradientType::Radial, "Radial"}};
  return m[type];
}

std::string SerializeUtils::PathFillTypeToString(PathFillType type) {
  static std::unordered_map<PathFillType, const char*> m = {
      {PathFillType::Winding, "Winding"},
      {PathFillType::EvenOdd, "EvenOdd"},
      {PathFillType::InverseWinding, "InverseWinding"},
      {PathFillType::InverseEvenOdd, "InverseEvenOdd"}};
  return m[type];
}

void SerializeUtils::SerializeBegin(flexbuffers::Builder& fbb, const std::string& type,
                                    size_t& mapStart, size_t& contentStart) {
  mapStart = fbb.StartMap();
  fbb.Key("Type");
  fbb.String(type);
  fbb.Key("Content");
  contentStart = fbb.StartMap();
}

void SerializeUtils::SerializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart) {
  fbb.EndMap(contentStart);
  fbb.EndMap(mapStart);
  fbb.Finish();
}

uint64_t SerializeUtils::GetObjID() {
  static uint64_t objID = 0;
  return objID++;
}

void SerializeUtils::FillComplexObjSerMap(const Matrix& matrix, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [matrix]() { return MatrixSerialization::Serialize(&matrix); };
}

void SerializeUtils::FillComplexObjSerMap(const Point& point, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [point]() { return PointSerialization::Serialize(&point); };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerFilter>>& filters,
                             uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [filters, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < filters.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& filter = filters[i];
      auto filterID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(),
                                       reinterpret_cast<uint64_t>(filter.get()), true,
                                       filter != nullptr, filterID);
      FillComplexObjSerMap(filter, filterID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<LayerFilter>& layerFilter, uint64_t objID,
                             ComplexObjSerMap* map) {
  if (layerFilter == nullptr) {
    return;
  }
  (*map)[objID] = [layerFilter, map]() {
    return LayerFilterSerialization::Serialize(layerFilter.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Layer>& layer, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* renderMap) {
  if (layer == nullptr) {
    return;
  }
  (*map)[objID] = [layer, map, renderMap]() { return LayerSerialization::SerializeLayer(layer.get(), map, renderMap); };
}

void SerializeUtils::FillComplexObjSerMap(const Rect& rect, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [rect]() { return RectSerialization::Serialize(&rect); };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<Layer>>& children, uint64_t objID,
                             ComplexObjSerMap* map, RenderableObjSerMap* renderMap) {
  (*map)[objID] = [children, map, renderMap]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < children.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& child = children[i];
      auto childID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(),
                                       reinterpret_cast<uint64_t>(child.get()), true,
                                       child != nullptr, childID);
      FillComplexObjSerMap(child, childID, map, renderMap);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerStyle>>& layerStyles,
                             uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [layerStyles, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < layerStyles.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& layerStyle = layerStyles[i];
      auto layerStyleID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(),
                                       reinterpret_cast<uint64_t>(layerStyle.get()), true,
                                       layerStyle != nullptr, layerStyleID);
      FillComplexObjSerMap(layerStyle, layerStyleID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<LayerStyle>& layerStyle, uint64_t objID,
                             ComplexObjSerMap* map) {
  if (layerStyle == nullptr) {
    return;
  }
  (*map)[objID] = [layerStyle, map]() {
    return LayerStyleSerialization::Serialize(layerStyle.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const SamplingOptions& sampling, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [sampling]() { return SamplingOptionsSerialization::Serialize(&sampling); };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Image>& image, uint64_t objID, ComplexObjSerMap* map) {
  if (image == nullptr) {
    return;
  }
  (*map)[objID] = [image]() { return ImageSerialization::Serialize(image.get()); };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Shape>& shape, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap) {
  if (shape == nullptr) {
    return;
  }
  (*map)[objID] = [shape, map, rosMap]() { return ShapeSerialization::Serialize(shape.get(), map, rosMap); };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<ShapeStyle>>& shapeStyles,
                             uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap) {
  (*map)[objID] = [shapeStyles, map, rosMap]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < shapeStyles.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& shapeStyle = shapeStyles[i];
      auto shapeStyleID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(),
                                       reinterpret_cast<uint64_t>(shapeStyle.get()), true,
                                       shapeStyle != nullptr, shapeStyleID);
      FillComplexObjSerMap(shapeStyle, shapeStyleID, map, rosMap);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<ShapeStyle>& shapeStyle, uint64_t objID,
                             ComplexObjSerMap* map, RenderableObjSerMap* rosMap) {
  if (shapeStyle == nullptr) {
    return;
  }
  (*map)[objID] = [shapeStyle, map, rosMap]() {
    return ShapeStyleSerialization::Serialize(shapeStyle.get(), map, rosMap);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<ColorFilter>& colorFilter, uint64_t objID,
                             ComplexObjSerMap* map) {
  if (colorFilter == nullptr) {
    return;
  }
  (*map)[objID] = [colorFilter, map]() {
    return ColorFilterSerialization::Serialize(colorFilter.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Typeface>& typeFace, uint64_t objID, ComplexObjSerMap* map) {
  if (typeFace == nullptr) {
    return;
  }
  (*map)[objID] = [typeFace]() { return TypeFaceSerialization::Serialize(typeFace.get()); };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<ImageFilter>& imageFilter, uint64_t objID,
                             ComplexObjSerMap* map) {
  if (imageFilter == nullptr) {
    return;
  }
  (*map)[objID] = [imageFilter, map]() {
    return ImageFilterSerialization::Serialize(imageFilter.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<RuntimeEffect>& runtimeEffect, uint64_t objID,
                             ComplexObjSerMap* map) {
  if (runtimeEffect == nullptr) {
    return;
  }
  (*map)[objID] = [runtimeEffect]() {
    return RuntimeEffectSerialization::Serialize(runtimeEffect.get());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Shader>& shader, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap) {
  if (shader == nullptr) {
    return;
  }
  (*map)[objID] = [shader, map, rosMap]() { return ShaderSerialization::Serialize(shader.get(), map, rosMap); };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<TextBlob> &textBlob, uint64_t objID,
  ComplexObjSerMap *map, RenderableObjSerMap* rosMap) {
  if(textBlob == nullptr) {
    return;
  }
  (*map)[objID] = [textBlob, map, rosMap]() {
    return TextBlobSerialization::Serialize(textBlob.get(), map, rosMap);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<float>& floatVec, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [floatVec]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < floatVec.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      auto value = floatVec[i];
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), value);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::array<float, 20>& matrix, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [matrix]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < 20; i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      auto value = matrix[i];
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), value);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<GlyphRun>& glyphRuns, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [glyphRuns, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < glyphRuns.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& glyphRun = glyphRuns[i];
      auto glyphRunID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), "", false, true, glyphRunID);
      FillComplexObjSerMap(glyphRun, glyphRunID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<GlyphID>& glyphs, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [glyphs]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < glyphs.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      auto value = glyphs[i];
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), static_cast<uint32_t>(value));
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<Point>& points, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [points, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < points.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      auto point = points[i];
      auto pointID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), "", false, true, pointID);
      FillComplexObjSerMap(point, pointID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<ImageFilter>>& imageFilters,
                             uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [imageFilters, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < imageFilters.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      const auto& imageFilter = imageFilters[i];
      auto imageFilterID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(),
                                       reinterpret_cast<uint64_t>(imageFilter.get()), true,
                                       imageFilter != nullptr, imageFilterID);
      FillComplexObjSerMap(imageFilter, imageFilterID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<Color>& colors, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [colors, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
    for (size_t i = 0; i < colors.size(); i++) {
      std::stringstream ss;
      ss << "[" << i << "]";
      auto color = colors[i];
      auto colorID = SerializeUtils::GetObjID();
      SerializeUtils::SetFlexBufferMap(fbb, ss.str().c_str(), "", false, true, colorID);
      FillComplexObjSerMap(color, colorID, map);
    }
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

bool SerializeUtils::CreateGLTexture(Context* context, int width, int height,
    GLTextureInfo* texture) {
  texture->target = GL_TEXTURE_2D;
  texture->format = GL_RGBA8;
  auto gl = GLFunctions::Get(context);
  gl->genTextures(1, &texture->id);
  if (texture->id <= 0) {
    return false;
  }
  gl->bindTexture(texture->target, texture->id);
  gl->texParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->texImage2D(texture->target, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  gl->bindTexture(texture->target, 0);
  return true;
}

void SerializeUtils::FillRenderableObjSerMap(const std::shared_ptr<Shape>& shape, uint64_t objID,
                                             RenderableObjSerMap* map) {
  (*map)[objID] = [shape](Context* context) {
    GLTextureInfo textureInfo;
    auto bounds = shape->getBounds();
    int width = static_cast<int>(bounds.width()) + padding * 2;
    int height = static_cast<int>(bounds.height()) + padding * 2;
    auto transformShape = Shape::ApplyMatrix(shape, Matrix::MakeTrans(padding-bounds.x(), padding-bounds.y()));
    CreateGLTexture(context, width, height, &textureInfo);
    auto surface = Surface::MakeFrom(context, {textureInfo, width,
      height}, ImageOrigin::BottomLeft);
    auto canvas = surface->getCanvas();
    canvas->clear();
    auto paint = Paint();
    canvas->drawShape(transformShape, paint);
    ImageInfo info = ImageInfo::Make(width, height, ColorType::RGBA_8888);
    std::vector<uint8_t> data(static_cast<size_t>(width * height * 4));
    bool result = surface->readPixels(info, data.data());
    auto gl = GLFunctions::Get(context);
    gl->deleteTextures(1, &textureInfo.id);
    if(!result) {
      return Data::MakeEmpty();
    }
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "ImageData", startMap, contentMap);
    fbb.Int("width", width);
    fbb.Int("height", height);
    fbb.Blob("data", data.data(), data.size());
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillRenderableObjSerMap(const std::shared_ptr<Image> &image, uint64_t objID,
  RenderableObjSerMap *map) {
  (*map)[objID] = [image](Context* context) {
    GLTextureInfo textureInfo;
    int width = image->width() + padding * 2;
    int height = image->height() + padding * 2;
    CreateGLTexture(context, width, height, &textureInfo);
    auto surface = Surface::MakeFrom(context, {textureInfo, width,
      height}, ImageOrigin::BottomLeft);
    auto canvas = surface->getCanvas();
    canvas->clear();
    canvas->drawImage(image, padding, padding);
    ImageInfo info = ImageInfo::Make(width, height, ColorType::RGBA_8888);
    std::vector<uint8_t> data(static_cast<size_t>(width * height * 4));
    bool result = surface->readPixels(info, data.data());
    auto gl = GLFunctions::Get(context);
    gl->deleteTextures(1, &textureInfo.id);
    if(!result) {
      return Data::MakeEmpty();
    }
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "ImageData", startMap, contentMap);
    fbb.Int("width", width);
    fbb.Int("height", height);
    fbb.Blob("data", data.data(), data.size());
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillRenderableObjSerMap(const Path &path, uint64_t objID, RenderableObjSerMap *map) {
    (*map)[objID] = [path](Context* context) {
      GLTextureInfo textureInfo;
      auto bounds = path.getBounds();
      int width = static_cast<int>(bounds.width()) + padding * 2;
      int height = static_cast<int>(bounds.height()) + padding * 2;
      CreateGLTexture(context, width, height, &textureInfo);
      auto tempPath = path;
      tempPath.transform(Matrix::MakeTrans(padding-bounds.x(), padding-bounds.y()));
      auto surface = Surface::MakeFrom(context, {textureInfo, width,
        height}, ImageOrigin::BottomLeft);
      auto canvas = surface->getCanvas();
      canvas->clear();
      auto paint = Paint();
      canvas->drawPath(tempPath, paint);
      ImageInfo info = ImageInfo::Make(width, height, ColorType::RGBA_8888);
      std::vector<uint8_t> data(static_cast<size_t>(width * height * 4));
      bool result = surface->readPixels(info, data.data());
      auto gl = GLFunctions::Get(context);
      gl->deleteTextures(1, &textureInfo.id);
      if(!result) {
        return Data::MakeEmpty();
      }
      flexbuffers::Builder fbb;
      size_t startMap;
      size_t contentMap;
      SerializeUtils::SerializeBegin(fbb, "ImageData", startMap, contentMap);
      fbb.Int("width", width);
      fbb.Int("height", height);
      fbb.Blob("data", data.data(), data.size());
      SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
      return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
    };
}

void SerializeUtils::FillRenderableObjSerMap(const std::shared_ptr<TextBlob> &textBlob, uint64_t objID,
  RenderableObjSerMap *map) {
  (*map)[objID] = [textBlob](Context* context) {
    GLTextureInfo textureInfo;
    auto bounds = textBlob->getBounds();
    int width = static_cast<int>(bounds.width()) + padding * 2;
    int height = static_cast<int>(bounds.height()) + padding * 2;
    CreateGLTexture(context, width, height, &textureInfo);
    auto surface = Surface::MakeFrom(context, {textureInfo, width,
      height}, ImageOrigin::BottomLeft);
    auto canvas = surface->getCanvas();
    canvas->clear();
    auto paint = Paint();
    canvas->drawTextBlob(textBlob, padding-bounds.x(), padding-bounds.y(), paint);
    ImageInfo info = ImageInfo::Make(width, height, ColorType::RGBA_8888);
    std::vector<uint8_t> data(static_cast<size_t>(width * height * 4));
    bool result = surface->readPixels(info, data.data());
    auto gl = GLFunctions::Get(context);
    gl->deleteTextures(1, &textureInfo.id);
    if(!result) {
      return Data::MakeEmpty();
    }
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, "ImageData", startMap, contentMap);
    fbb.Int("width", width);
    fbb.Int("height", height);
    fbb.Blob("data", data.data(), data.size());
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const Color& color, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [color]() { return ColorSerialization::Serialize(&color); };
}

void SerializeUtils::FillComplexObjSerMap(const Font& font, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [font, map]() { return FontSerialization::Serialize(&font, map); };
}

void SerializeUtils::FillComplexObjSerMap(const FontMetrics& fontMetrics, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [fontMetrics]() { return FontMetricsSerialization::Serialize(&fontMetrics); };
}

void SerializeUtils::FillComplexObjSerMap(const GlyphRun& glyphRun, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [glyphRun, map]() { return glyphRunSerialization::Serialize(&glyphRun, map); };
}

void SerializeUtils::FillComplexObjSerMap(const Path& path, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [path, map]() { return PathSerialization::Serialize(&path, map); };
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, const char* value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.String("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, std::string value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.String("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, int value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.Int("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key,
                                      unsigned int value, bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.UInt("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, uint64_t value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.UInt("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, float value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.Float("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, double value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.Double("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}

void SerializeUtils::SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, bool value,
                                      bool isAddress, bool isExpandable,
                                      std::optional<uint64_t> objID, bool isRenderableObj) {
  std::ostringstream ss;
  ss << std::setw(8) << std::setfill('0') << insertionCounter++;
  fbb.Key(ss.str() + "_" + key);
  fbb.Map([&]() {
    fbb.Bool("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
    objID ? fbb.UInt("objID", objID.value()) : fbb.Null("objID");
    fbb.Bool("IsRenderableObj", isRenderableObj);
  });
}
}  // namespace tgfx
#endif