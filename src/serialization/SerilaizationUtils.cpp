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

#include "SerializationUtils.h"
#include "core/images/FilterImage.h"

namespace tgfx {
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
      {Types::ShapeType::Glyph, "Glyph"},   {Types::ShapeType::Inverse, "Inverse"},
      {Types::ShapeType::Matrix, "Matrix"}, {Types::ShapeType::Merge, "Merge"},
      {Types::ShapeType::Path, "Path"},     {Types::ShapeType::Provider, "Provider"},
      {Types::ShapeType::Stroke, "Stroke"}};
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
      {Types::LayerFilterType::ColorMatrixFliter, "ColorMatrixFilter"},
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

template <>
void SerializeUtils::SetFlexBufferMap<const char*>(flexbuffers::Builder& fbb, const char* key,
                                                   const char* value, bool isAddress,
                                                   bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.String("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<std::string>(flexbuffers::Builder& fbb, const char* key,
                                                   std::string value, bool isAddress,
                                                   bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.String("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<int>(flexbuffers::Builder& fbb, const char* key, int value,
                                           bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Int("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<unsigned int>(flexbuffers::Builder& fbb, const char* key,
                                                    unsigned int value, bool isAddress,
                                                    bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.UInt("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<uint64_t>(flexbuffers::Builder& fbb, const char* key,
                                                uint64_t value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.UInt("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<float>(flexbuffers::Builder& fbb, const char* key,
                                             float value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Float("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<double>(flexbuffers::Builder& fbb, const char* key,
                                              double value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Double("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::SetFlexBufferMap<bool>(flexbuffers::Builder& fbb, const char* key, bool value,
                                            bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Bool("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

}  // namespace tgfx
#endif