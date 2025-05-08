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
#ifdef TGFX_ENABLE_PROFILING

#include "SerializationUtils.h"
#include "core/images/FilterImage.h"

namespace tgfx {
std::string SerializeUtils::layerTypeToString(LayerType type) {
  static std::unordered_map<LayerType, const char*> m = {
      {LayerType::Layer, "Layer"},      {LayerType::Image, "ImageLayer"},
      {LayerType::Shape, "ShapeLayer"}, {LayerType::Gradient, "GradientLayer"},
      {LayerType::Text, "TextLayer"},   {LayerType::Solid, "SolidLayer"}};
  return m[type];
}
std::string SerializeUtils::blendModeToString(BlendMode mode) {
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
std::string SerializeUtils::strokeAlignToString(StrokeAlign align) {
  static std::unordered_map<StrokeAlign, const char*> m = {{StrokeAlign::Center, "Center"},
                                                           {StrokeAlign::Inside, "Inside"},
                                                           {StrokeAlign::Outside, "Outside"}};
  return m[align];
}
std::string SerializeUtils::textAlignToString(TextAlign align) {
  static std::unordered_map<TextAlign, const char*> m = {{TextAlign::Left, "Left"},
                                                         {TextAlign::Right, "Right"},
                                                         {TextAlign::Center, "Center"},
                                                         {TextAlign::Justify, "Justify"}};
  return m[align];
}
std::string SerializeUtils::tileModeToString(TileMode tileMode) {
  static std::unordered_map<TileMode, const char*> m = {{TileMode::Clamp, "Clamp"},
                                                        {TileMode::Repeat, "Repeat"},
                                                        {TileMode::Mirror, "Mirror"},
                                                        {TileMode::Decal, "Decal"}};
  return m[tileMode];
}

std::string SerializeUtils::imageTypeToString(Image::Type type) {
  static std::unordered_map<Image::Type, const char*> m = {
      {Image::Type::Buffer, "Buffer"},         {Image::Type::Codec, "Codec"},
      {Image::Type::Decoded, "Decoded"},       {Image::Type::Filter, "Filter"},
      {Image::Type::Generator, "Generator"},   {Image::Type::Mipmap, "Mipmap"},
      {Image::Type::Orient, "Orient"},         {Image::Type::Picture, "Picture"},
      {Image::Type::Rasterized, "Rasterized"}, {Image::Type::RGBAAA, "RGBAA"},
      {Image::Type::Texture, "Texture"},       {Image::Type::Subset, "Subset"}};
  return m[type];
}

std::string SerializeUtils::filterModeToString(FilterMode mode) {
  static std::unordered_map<FilterMode, const char*> m = {{FilterMode::Linear, "Linear"},
                                                          {FilterMode::Nearest, "Nearest"}};
  return m[mode];
}

std::string SerializeUtils::mipmapModeToString(MipmapMode mode) {
  static std::unordered_map<MipmapMode, const char*> m = {{MipmapMode::Linear, "Linear"},
                                                          {MipmapMode::Nearest, "Nearest"},
                                                          {MipmapMode::None, "None"}};
  return m[mode];
}

std::string SerializeUtils::shapeTypeToString(Shape::Type type) {
  static std::unordered_map<Shape::Type, const char*> m = {
      {Shape::Type::Append, "Append"}, {Shape::Type::Effect, "Effect"},
      {Shape::Type::Glyph, "Glyph"},   {Shape::Type::Inverse, "Inverse"},
      {Shape::Type::Matrix, "Matrix"}, {Shape::Type::Merge, "Merge"},
      {Shape::Type::Path, "Path"},     {Shape::Type::Provider, "Provider"},
      {Shape::Type::Stroke, "Stroke"}};
  return m[type];
}
std::string SerializeUtils::shaderTypeToString(Shader::Type type) {
  static std::unordered_map<Shader::Type, const char*> m = {
      {Shader::Type::Color, "Color"},   {Shader::Type::ColorFilter, "ColorFilter"},
      {Shader::Type::Image, "Image"},   {Shader::Type::Blend, "Blend"},
      {Shader::Type::Matrix, "Matrix"}, {Shader::Type::Gradient, "Gradient"}};
  return m[type];
}
std::string SerializeUtils::lineCapToString(LineCap lineCap) {
  static std::unordered_map<LineCap, const char*> m = {{LineCap::Butt, "Butt"},
                                                       {LineCap::Round, "Round"},
                                                       {LineCap::Square, "Square"}};
  return m[lineCap];
}
std::string SerializeUtils::lineJoinToString(LineJoin lineJoin) {
  static std::unordered_map<LineJoin, const char*> m = {{LineJoin::Miter, "Miter"},
                                                        {LineJoin::Round, "Round"},
                                                        {LineJoin::Bevel, "Bevel"}};
  return m[lineJoin];
}
std::string SerializeUtils::imageFilterTypeToString(ImageFilter::Type type) {
  static std::unordered_map<ImageFilter::Type, const char*> m = {
      {ImageFilter::Type::Blur, "Blur"},
      {ImageFilter::Type::Color, "Color"},
      {ImageFilter::Type::Compose, "Compose"},
      {ImageFilter::Type::Runtime, "Runtime"},
      {ImageFilter::Type::DropShadow, "DropShadow"},
      {ImageFilter::Type::InnerShadow, "InnerShadow"}};
  return m[type];
}
std::string SerializeUtils::colorFilterTypeToString(ColorFilter::Type type) {
  static std::unordered_map<ColorFilter::Type, const char*> m = {
      {ColorFilter::Type::Blend, "Blend"},
      {ColorFilter::Type::Matrix, "Matrix"},
      {ColorFilter::Type::AlphaThreshold, "AlphaThreshold"},
      {ColorFilter::Type::Compose, "Compose"}};
  return m[type];
}
void SerializeUtils::serializeBegin(flexbuffers::Builder& fbb, const std::string& type,
                                    size_t& mapStart, size_t& contentStart) {
  mapStart = fbb.StartMap();
  fbb.Key("Type");
  fbb.String(type);
  fbb.Key("Content");
  contentStart = fbb.StartMap();
}
void SerializeUtils::serializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart) {
  fbb.EndMap(contentStart);
  fbb.EndMap(mapStart);
  fbb.Finish();
}

template <>
void SerializeUtils::setFlexBufferMap<const char*>(flexbuffers::Builder& fbb, const char* key,
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
void SerializeUtils::setFlexBufferMap<std::string>(flexbuffers::Builder& fbb, const char* key,
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
void SerializeUtils::setFlexBufferMap<int>(flexbuffers::Builder& fbb, const char* key, int value,
                                           bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Int("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::setFlexBufferMap<unsigned int>(flexbuffers::Builder& fbb, const char* key,
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
void SerializeUtils::setFlexBufferMap<uint64_t>(flexbuffers::Builder& fbb, const char* key,
                                                uint64_t value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.UInt("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::setFlexBufferMap<float>(flexbuffers::Builder& fbb, const char* key,
                                             float value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Float("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::setFlexBufferMap<double>(flexbuffers::Builder& fbb, const char* key,
                                              double value, bool isAddress, bool isExpandable) {
  fbb.Key(key);
  fbb.Map([&]() {
    fbb.Double("Value", value);
    fbb.Bool("IsExpandable", isExpandable);
    fbb.Bool("IsAddress", isAddress);
  });
}

template <>
void SerializeUtils::setFlexBufferMap<bool>(flexbuffers::Builder& fbb, const char* key, bool value,
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