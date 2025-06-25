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
#include <tgfx/gpu/Context.h>
#include <string>

namespace tgfx {
class SerializeUtils {
 public:
  using ComplexObjSerMap = std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>;
  using RenderableObjSerMap = std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>(Context*)>>;

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

  static std::string RecordedContentTypeToString(Types::RecordedContentType type);

  static void SerializeBegin(flexbuffers::Builder& fbb, const std::string& type, size_t& mapStart,
                             size_t& contentStart);

  static void SerializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart);

  static uint64_t GetObjID();

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, const char* value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, std::string value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, int value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, unsigned int value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, uint64_t value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, float value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, double value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, bool value,
                               bool isAddress = false, bool isExpandable = false,
                               std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

  static void FillComplexObjSerMap(const Matrix& matrix, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const Point& point, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const Rect& rect, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const SamplingOptions& sampling, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const Color& color, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const Font& font, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const FontMetrics& fontMetrics, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const GlyphRun& glyphRun, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const Path& path, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<LayerFilter>& layerFilter, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<Layer>& layer, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* renderMap);

  static void FillComplexObjSerMap(const std::shared_ptr<LayerStyle>& layerStyle, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<Image>& image, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<ShapeStyle>& shapeStyle, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap);

  static void FillComplexObjSerMap(const std::shared_ptr<ColorFilter>& colorFilter, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<Typeface>& typeFace, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<ImageFilter>& imageFilter, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<RuntimeEffect>& runtimeEffect, uint64_t objID,
                      ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::shared_ptr<Shader>& shader, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap);

  static void FillComplexObjSerMap(const std::shared_ptr<TextBlob>& textBlob, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap);

  static void FillComplexObjSerMap(const std::shared_ptr<Picture>& picture, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerFilter>>& filters, uint64_t objID,
                      ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<std::shared_ptr<Layer>>& children, uint64_t objID,
                      ComplexObjSerMap* map, RenderableObjSerMap* renderMap);

  static void FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerStyle>>& layerStyles, uint64_t objID,
                      ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<std::shared_ptr<ShapeStyle>>& shapeStyles, uint64_t objID,
                      ComplexObjSerMap* map, RenderableObjSerMap* rosMap);

  static void FillComplexObjSerMap(const std::vector<float>& floatVec, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::array<float, 20>& matrix, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<GlyphID>& glyphs, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<Point>& points, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<std::shared_ptr<ImageFilter>>& imageFilters, uint64_t objID,
                      ComplexObjSerMap* map);

  static void FillComplexObjSerMap(const std::vector<Color>& colors, uint64_t objID, ComplexObjSerMap* map);

  static void FillComplexObjSermap(const std::unique_ptr<RecordedContent>& recordedContent, uint64_t objID, ComplexObjSerMap* map, RenderableObjSerMap* rosMap);

  static bool CreateGLTexture(Context* context, int width, int height, GLTextureInfo* texture);

  static void FillRenderableObjSerMap(const std::shared_ptr<Shape>& shape, uint64_t objID, RenderableObjSerMap* map);

  static void FillRenderableObjSerMap(const std::shared_ptr<Image>& image, uint64_t objID, RenderableObjSerMap* map);

  static void FillRenderableObjSerMap(const Path& path, uint64_t objID, RenderableObjSerMap* map);

    static void FillRenderableObjSerMap(const std::shared_ptr<TextBlob>& textBlob, uint64_t objID, RenderableObjSerMap* map);

  static void FillRenderableObjSerMap(const std::shared_ptr<Picture>& picture, uint64_t objID, RenderableObjSerMap* map);
};
}  // namespace tgfx