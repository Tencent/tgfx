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

std::shared_ptr<Data> ColorFilterSerialization::Serialize(ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(colorFilter);
  switch (type) {
    case Types::ColorFilterType::Blend:
      SerializeModeColorFilterImpl(fbb, colorFilter);
      break;
    case Types::ColorFilterType::Compose:
      SerializeComposeColorFilterImpl(fbb, colorFilter);
      break;
    case Types::ColorFilterType::Matrix:
      SerializeMatrixColorFilterImpl(fbb, colorFilter);
      break;
    case Types::ColorFilterType::AlphaThreshold:
      SerializeAlphaThreadholdColorFilterImpl(fbb, colorFilter);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ColorFilterSerialization::SerializeColorFilterImpl(flexbuffers::Builder& fbb,
                                                        ColorFilter* colorFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "type", SerializeUtils::ColorFilterTypeToString(Types::Get(colorFilter)));
}

void ColorFilterSerialization::SerializeComposeColorFilterImpl(flexbuffers::Builder& fbb,
                                                               ColorFilter* colorFilter) {
  SerializeColorFilterImpl(fbb, colorFilter);
  ComposeColorFilter* composeColorFilter = static_cast<ComposeColorFilter*>(colorFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "inner",
                                   reinterpret_cast<uint64_t>(composeColorFilter->inner.get()),
                                   true, composeColorFilter->inner != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "outer",
                                   reinterpret_cast<uint64_t>(composeColorFilter->outer.get()),
                                   true, composeColorFilter->outer != nullptr);
}

void ColorFilterSerialization::SerializeAlphaThreadholdColorFilterImpl(flexbuffers::Builder& fbb,
                                                                       ColorFilter* colorFilter) {
  SerializeColorFilterImpl(fbb, colorFilter);
  AlphaThresholdColorFilter* alphaThresholdColorFilter =
      static_cast<AlphaThresholdColorFilter*>(colorFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "threshold", alphaThresholdColorFilter->threshold);
}

void ColorFilterSerialization::SerializeMatrixColorFilterImpl(flexbuffers::Builder& fbb,
                                                              ColorFilter* colorFilter) {
  SerializeColorFilterImpl(fbb, colorFilter);
  MatrixColorFilter* matrixColorFilter = static_cast<MatrixColorFilter*>(colorFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", 20, false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "alphaIsUnchanged", matrixColorFilter->alphaIsUnchanged);
}

void ColorFilterSerialization::SerializeModeColorFilterImpl(flexbuffers::Builder& fbb,
                                                            ColorFilter* colorFilter) {
  SerializeColorFilterImpl(fbb, colorFilter);
  ModeColorFilter* modeColorFilter = static_cast<ModeColorFilter*>(colorFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "mode",
                                   SerializeUtils::BlendModeToString(modeColorFilter->mode));
}
}  // namespace tgfx
#endif