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

#pragma once

#include <core/utils/Types.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#define FLATBUFFERS_LOCALE_INDEPENDENT 0
#include <flatbuffers/flexbuffers.h>
#pragma clang diagnostic pop
#include <string>
#include "inspect/Protocol.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
namespace SerializeUtils {
using ComplexObjSerMap = std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>;
using RenderableObjSerMap =
    std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>(Context*)>>;

std::string LayerTypeToString(LayerType type);

std::string BlendModeToString(BlendMode mode);

std::string TileModeToString(TileMode tileMode);

std::string LayerFilterTypeToString(Types::LayerFilterType type);

std::string LayerStyleTypeToString(LayerStyleType type);

std::string LayerStylePositionToString(LayerStylePosition position);

std::string LayerStyleExtraSourceTypeToString(LayerStyleExtraSourceType type);

void SerializeBegin(flexbuffers::Builder& fbb, tgfx::inspect::LayerTreeMessage type,
                    size_t& mapStart, size_t& contentStart);

void SerializeEnd(flexbuffers::Builder& fbb, size_t mapStart, size_t contentStart);

uint64_t GetObjID();

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, const char* value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, std::string value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, int value, bool isAddress = false,
                      bool isExpandable = false, std::optional<uint64_t> objID = std::nullopt,
                      bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, unsigned int value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, uint64_t value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, float value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, double value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void SetFlexBufferMap(flexbuffers::Builder& fbb, const char* key, bool value,
                      bool isAddress = false, bool isExpandable = false,
                      std::optional<uint64_t> objID = std::nullopt, bool isRenderableObj = false);

void FillComplexObjSerMap(const Matrix& matrix, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const Point& point, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const Rect& rect, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const Color& color, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::shared_ptr<LayerFilter>& layerFilter, uint64_t objID,
                          ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::shared_ptr<Layer>& layer, uint64_t objID,
                          ComplexObjSerMap* map, RenderableObjSerMap* renderMap);

void FillComplexObjSerMap(const std::shared_ptr<LayerStyle>& layerStyle, uint64_t objID,
                          ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::shared_ptr<Picture>& picture, uint64_t objID,
                          ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerFilter>>& filters, uint64_t objID,
                          ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::vector<std::shared_ptr<Layer>>& children, uint64_t objID,
                          ComplexObjSerMap* map, RenderableObjSerMap* renderMap);

void FillComplexObjSerMap(const std::vector<std::shared_ptr<LayerStyle>>& layerStyles,
                          uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::array<float, 20>& matrix, uint64_t objID,
                          ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::vector<Point>& points, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSerMap(const std::vector<Color>& colors, uint64_t objID, ComplexObjSerMap* map);

void FillComplexObjSermap(LayerContent* recordedContent, uint64_t objID, ComplexObjSerMap* map,
                          RenderableObjSerMap* rosMap);

void FillRenderableObjSerMap(const std::shared_ptr<Picture>& picture, uint64_t objID,
                             RenderableObjSerMap* map);
};  // namespace SerializeUtils
}  // namespace tgfx