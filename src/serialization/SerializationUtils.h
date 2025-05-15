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

#pragma once

#include <core/utils/Types.h>
#include <flatbuffers/flexbuffers.h>
#include <tgfx/core/GradientType.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/TextAlign.h>
#include <string>

namespace tgfx {
class SerializeUtils {
 public:
  using Map = std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>;

  static std::string LayerTypeToString(LayerType type);

  static std::string BlendModeToString(BlendMode mode);

  static std::string StrokeAlignToString(StrokeAlign align);

  static std::string TextAlignToString(TextAlign align);

  static std::string TileModeToString(TileMode tileMode);

  static std::string ImageTypeToString(Types::ImageType type);

  static std::string FilterModeToString(FilterMode mode);

  static std::string MipmapModeToString(MipmapMode mode);

  static std::string ShapeTypeToString(Types::ShapeType type);

  static std::string ShaderTypeToString(Types::ShaderType type);

  static std::string LineCapToString(LineCap lineCap);

  static std::string LineJoinToString(LineJoin lineJoin);

  static std::string ImageFilterTypeToString(Types::ImageFilterType type);

  static std::string ColorFilterTypeToString(Types::ColorFilterType type);

  static std::string LayerFilterTypeToString(Types::LayerFilterType type);

  static std::string LayerStyleTypeToString(LayerStyleType type);

  static std::string LayerStylePositionToString(LayerStylePosition position);

  static std::string LayerStyleExtraSourceTypeToString(LayerStyleExtraSourceType type);

  static std::string ShapeStyleTypeToString(Types::ShapeStyleType type);

  static std::string GradientTypeToString(GradientType type);

  static std::string PathFillTypeToString(PathFillType type);

  static void SerializeBegin(flexbuffers::Builder& fbb, const std::string& type, size_t& mapStart,
                             size_t& contentStart);

  static void SerializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart);

  static uint64_t GetObjID();

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, const char* value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, std::string value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, int value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, unsigned int value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, uint64_t value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, float value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, double value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, bool value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt);

  static void FillMap(const Matrix& matrix, uint64_t objID, Map* map);

  static void FillMap(const Point& point, uint64_t objID, Map* map);

  static void FillMap(const Rect& rect, uint64_t objID, Map* map);

  static void FillMap(const SamplingOptions& sampling, uint64_t objID, Map* map);

  static void FillMap(const Color& color, uint64_t objID, Map* map);

  static void FillMap(const Font& font, uint64_t objID, Map* map);

  static void FillMap(const FontMetrics& fontMetrics, uint64_t objID, Map* map);

  static void FillMap(const GlyphRun& glyphRun, uint64_t objID, Map* map);

  static void FillMap(const Path& path, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<LayerFilter>& layerFilter, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<Layer>& layer, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<LayerStyle>& layerStyle, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<Image>& image, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<Shape>& shape, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<ShapeStyle>& shapeStyle, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<ColorFilter>& colorFilter, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<Typeface>& typeFace, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<GlyphFace>& glyphFace, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<ImageFilter>& imageFilter, uint64_t objID, Map* map);

  static void FillMap(const std::shared_ptr<RuntimeEffect>& runtimeEffect, uint64_t objID,
                      Map* map);

  static void FillMap(const std::shared_ptr<Shader>& shader, uint64_t objID, Map* map);

  static void FillMap(const std::vector<std::shared_ptr<LayerFilter>>& filters, uint64_t objID,
                      Map* map);

  static void FillMap(const std::vector<std::shared_ptr<Layer>>& children, uint64_t objID,
                      Map* map);

  static void FillMap(const std::vector<std::shared_ptr<LayerStyle>>& layerStyles, uint64_t objID,
                      Map* map);

  static void FillMap(const std::vector<std::shared_ptr<ShapeStyle>>& shapeStyles, uint64_t objID,
                      Map* map);

  static void FillMap(const std::vector<float>& floatVec, uint64_t objID, Map* map);

  static void FillMap(const std::array<float, 20>& matrix, uint64_t objID, Map* map);

  static void FillMap(const std::vector<GlyphRun>& glyphRuns, uint64_t objID, Map* map);

  static void FillMap(const std::vector<GlyphID>& glyphs, uint64_t objID, Map* map);

  static void FillMap(const std::vector<Point>& points, uint64_t objID, Map* map);

  static void FillMap(const std::vector<std::shared_ptr<ImageFilter>>& imageFilters, uint64_t objID,
                      Map* map);

  static void FillMap(const std::vector<Color>& colors, uint64_t objID, Map* map);
};
}  // namespace tgfx