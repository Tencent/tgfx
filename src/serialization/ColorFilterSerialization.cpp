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
#include "core/filters/AlphaThresholdColorFilter.h"
#include "core/filters/ComposeColorFilter.h"
#include "core/filters/MatrixColorFilter.h"
#include "core/filters/ModeColorFilter.h"
#include "core/utils/Types.h"

namespace tgfx {

std::shared_ptr<Data> ColorFilterSerialization::Serialize(const ColorFilter* colorFilter,
                                                          SerializeUtils::MapRef map) {
  DEBUG_ASSERT(colorFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(colorFilter);
  switch (type) {
    case Types::ColorFilterType::Blend:
      SerializeModeColorFilterImpl(fbb, colorFilter, map);
      break;
    case Types::ColorFilterType::Compose:
      SerializeComposeColorFilterImpl(fbb, colorFilter, map);
      break;
    case Types::ColorFilterType::Matrix:
      SerializeMatrixColorFilterImpl(fbb, colorFilter, map);
      break;
    case Types::ColorFilterType::AlphaThreshold:
      SerializeAlphaThreadholdColorFilterImpl(fbb, colorFilter);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ColorFilterSerialization::SerializeColorFilterImpl(flexbuffers::Builder& fbb,
                                                        const ColorFilter* colorFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "type", SerializeUtils::ColorFilterTypeToString(Types::Get(colorFilter)));
}

void ColorFilterSerialization::SerializeComposeColorFilterImpl(flexbuffers::Builder& fbb,
                                                               const ColorFilter* colorFilter,
                                                               SerializeUtils::MapRef map) {
  SerializeColorFilterImpl(fbb, colorFilter);
  const ComposeColorFilter* composeColorFilter =
      static_cast<const ComposeColorFilter*>(colorFilter);

  auto innerID = SerializeUtils::GetObjID();
  auto inner = composeColorFilter->inner;
  SerializeUtils::SetFlexBufferMap(fbb, "inner", reinterpret_cast<uint64_t>(inner.get()), true,
                                   inner != nullptr, innerID);
  SerializeUtils::FillMap(inner, innerID, map);

  auto outerID = SerializeUtils::GetObjID();
  auto outer = composeColorFilter->outer;
  SerializeUtils::SetFlexBufferMap(fbb, "outer", reinterpret_cast<uint64_t>(outer.get()), true,
                                   outer != nullptr, outerID);
  SerializeUtils::FillMap(outer, outerID, map);
}

void ColorFilterSerialization::SerializeAlphaThreadholdColorFilterImpl(
    flexbuffers::Builder& fbb, const ColorFilter* colorFilter) {
  SerializeColorFilterImpl(fbb, colorFilter);
  const AlphaThresholdColorFilter* alphaThresholdColorFilter =
      static_cast<const AlphaThresholdColorFilter*>(colorFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "threshold", alphaThresholdColorFilter->threshold);
}

void ColorFilterSerialization::SerializeMatrixColorFilterImpl(flexbuffers::Builder& fbb,
                                                              const ColorFilter* colorFilter,
                                                              SerializeUtils::MapRef map) {
  SerializeColorFilterImpl(fbb, colorFilter);
  const MatrixColorFilter* matrixColorFilter = static_cast<const MatrixColorFilter*>(colorFilter);

  auto matrixID = SerializeUtils::GetObjID();
  auto matrix = matrixColorFilter->matrix;
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", 20, false, true, matrixID);
  SerializeUtils::FillMap(matrix, matrixID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "alphaIsUnchanged", matrixColorFilter->alphaIsUnchanged);
}

void ColorFilterSerialization::SerializeModeColorFilterImpl(flexbuffers::Builder& fbb,
                                                            const ColorFilter* colorFilter,
                                                            SerializeUtils::MapRef map) {
  SerializeColorFilterImpl(fbb, colorFilter);
  const ModeColorFilter* modeColorFilter = static_cast<const ModeColorFilter*>(colorFilter);
  auto colorID = SerializeUtils::GetObjID();
  auto color = modeColorFilter->color;
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "mode",
                                   SerializeUtils::BlendModeToString(modeColorFilter->mode));
}
}  // namespace tgfx
#endif