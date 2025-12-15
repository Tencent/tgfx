/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ColorSerialization.h"
#include "LayerFilterSerialization.h"
#include "LayerSerialization.h"
#include "LayerStyleSerialization.h"
#include "MatrixSerialization.h"
#include "PictureSerialization.h"
#include "PointSerialization.h"
#include "RecordedContentSerialization.h"
#include "RectSerialization.h"
#include "SerializationUtils.h"
#include "core/images/FilterImage.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

constexpr int padding = 20;
static int insertionCounter = 0;
std::string SerializeUtils::LayerTypeToString(LayerType type) {
  static std::unordered_map<LayerType, const char*> m = {{LayerType::Layer, "Layer"},
                                                         {LayerType::Image, "ImageLayer"},
                                                         {LayerType::Shape, "ShapeLayer"},
                                                         {LayerType::Text, "TextLayer"},
                                                         {LayerType::Solid, "SolidLayer"}};
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

std::string SerializeUtils::TileModeToString(TileMode tileMode) {
  static std::unordered_map<TileMode, const char*> m = {{TileMode::Clamp, "Clamp"},
                                                        {TileMode::Repeat, "Repeat"},
                                                        {TileMode::Mirror, "Mirror"},
                                                        {TileMode::Decal, "Decal"}};
  return m[tileMode];
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

void SerializeUtils::SerializeBegin(flexbuffers::Builder& fbb, tgfx::inspect::LayerTreeMessage type,
                                    size_t& mapStart, size_t& contentStart) {
  mapStart = fbb.StartMap();
  fbb.Key("Type");
  fbb.UInt(static_cast<uint8_t>(type));
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

void SerializeUtils::FillComplexObjSerMap(const Matrix& matrix, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [matrix]() { return MatrixSerialization::Serialize(&matrix); };
}

void SerializeUtils::FillComplexObjSerMap(const Point& point, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [point]() { return PointSerialization::Serialize(&point); };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerFilter>>& filters,
                                          uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [filters, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<LayerFilter>& layerFilter,
                                          uint64_t objID, ComplexObjSerMap* map) {
  if (layerFilter == nullptr) {
    return;
  }
  (*map)[objID] = [layerFilter, map]() {
    return LayerFilterSerialization::Serialize(layerFilter.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Layer>& layer, uint64_t objID,
                                          ComplexObjSerMap* map, RenderableObjSerMap* renderMap) {
  if (layer == nullptr) {
    return;
  }
  (*map)[objID] = [layer, map, renderMap]() {
    return LayerSerialization::SerializeLayer(layer.get(), map, renderMap);
  };
}

void SerializeUtils::FillComplexObjSerMap(const Rect& rect, uint64_t objID, ComplexObjSerMap* map) {
  (*map)[objID] = [rect]() { return RectSerialization::Serialize(&rect); };
}

void SerializeUtils::FillComplexObjSerMap(const std::vector<std::shared_ptr<Layer>>& children,
                                          uint64_t objID, ComplexObjSerMap* map,
                                          RenderableObjSerMap* renderMap) {
  (*map)[objID] = [children, map, renderMap]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSerMap(
    const std::vector<std::shared_ptr<LayerStyle>>& layerStyles, uint64_t objID,
    ComplexObjSerMap* map) {
  (*map)[objID] = [layerStyles, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<LayerStyle>& layerStyle,
                                          uint64_t objID, ComplexObjSerMap* map) {
  if (layerStyle == nullptr) {
    return;
  }
  (*map)[objID] = [layerStyle, map]() {
    return LayerStyleSerialization::Serialize(layerStyle.get(), map);
  };
}

void SerializeUtils::FillComplexObjSerMap(const std::shared_ptr<Picture>& picture, uint64_t objID,
                                          ComplexObjSerMap* map) {
  if (picture == nullptr) {
    return;
  }
  (*map)[objID] = [picture]() { return PictureSerialization::Serialize(picture.get()); };
}

void SerializeUtils::FillComplexObjSerMap(const std::array<float, 20>& matrix, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [matrix]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSerMap(const std::vector<Point>& points, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [points, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSerMap(const std::vector<Color>& colors, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [colors, map]() {
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute,
                                   startMap, contentMap);
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

void SerializeUtils::FillComplexObjSermap(LayerContent* recordedContent, uint64_t objID,
                                          ComplexObjSerMap* map, RenderableObjSerMap* rosMap) {
  (*map)[objID] = [recordedContent, map, rosMap]() {
    return RecordedContentSerialization::Serialize(recordedContent, map, rosMap);
  };
}

void SerializeUtils::FillRenderableObjSerMap(const std::shared_ptr<Picture>& picture,
                                             uint64_t objID, RenderableObjSerMap* map) {
  (*map)[objID] = [picture](Context* context) {
    auto bounds = picture->getBounds();
    int width = static_cast<int>(bounds.width()) + padding * 2;
    int height = static_cast<int>(bounds.height()) + padding * 2;
    auto surface = Surface::Make(context, width, height);
    auto canvas = surface->getCanvas();
    canvas->clear();
    auto paint = Paint();
    auto matrix = Matrix::MakeTrans(padding - bounds.x(), padding - bounds.y());
    canvas->drawPicture(picture, &matrix, &paint);
    ImageInfo info = ImageInfo::Make(width, height, ColorType::RGBA_8888);
    std::vector<uint8_t> data(static_cast<size_t>(width * height * 4));
    bool result = surface->readPixels(info, data.data());
    if (!result) {
      return Data::MakeEmpty();
    }
    flexbuffers::Builder fbb;
    size_t startMap;
    size_t contentMap;
    SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::ImageData, startMap,
                                   contentMap);
    fbb.Int("width", width);
    fbb.Int("height", height);
    fbb.Blob("data", data.data(), data.size());
    SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
    return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  };
}

void SerializeUtils::FillComplexObjSerMap(const Color& color, uint64_t objID,
                                          ComplexObjSerMap* map) {
  (*map)[objID] = [color]() { return ColorSerialization::Serialize(&color); };
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