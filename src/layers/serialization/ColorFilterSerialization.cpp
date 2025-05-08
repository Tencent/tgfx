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

#include "ColorFilterSerialization.h"
#include "core/filters/AlphaThresholdColorFilter.h"
#include "core/filters/ComposeColorFilter.h"
#include "core/filters/MatrixColorFilter.h"
#include "core/filters/ModeColorFilter.h"

namespace tgfx {

std::shared_ptr<Data> ColorFilterSerialization::serializeColorFilter(ColorFilter* colorFilter) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = colorFilter->type();
  switch (type) {
    case ColorFilter::Type::Blend:
      serializeModeColorFilterImpl(fbb, colorFilter);
      break;
    case ColorFilter::Type::Compose:
      serializeComposeColorFilterImpl(fbb, colorFilter);
      break;
    case ColorFilter::Type::Matrix:
      serializeMatrixColorFilterImpl(fbb, colorFilter);
      break;
    case ColorFilter::Type::AlphaThreshold:
      serializeAlphaThreadholdColorFilterImpl(fbb, colorFilter);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ColorFilterSerialization::serializeColorFilterImpl(flexbuffers::Builder& fbb,
                                                        ColorFilter* colorFilter) {
  SerializeUtils::setFlexBufferMap(fbb, "Type",
                                   SerializeUtils::colorFilterTypeToString(colorFilter->type()));
}
void ColorFilterSerialization::serializeComposeColorFilterImpl(flexbuffers::Builder& fbb,
                                                               ColorFilter* colorFilter) {
  serializeColorFilterImpl(fbb, colorFilter);
  ComposeColorFilter* composeColorFilter = static_cast<ComposeColorFilter*>(colorFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Inner",
                                   reinterpret_cast<uint64_t>(composeColorFilter->inner.get()),
                                   true, composeColorFilter->inner != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Outer",
                                   reinterpret_cast<uint64_t>(composeColorFilter->outer.get()),
                                   true, composeColorFilter->outer != nullptr);
}
void ColorFilterSerialization::serializeAlphaThreadholdColorFilterImpl(flexbuffers::Builder& fbb,
                                                                       ColorFilter* colorFilter) {
  serializeColorFilterImpl(fbb, colorFilter);
  AlphaThresholdColorFilter* alphaThresholdColorFilter =
      static_cast<AlphaThresholdColorFilter*>(colorFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Threshold", alphaThresholdColorFilter->threshold);
}
void ColorFilterSerialization::serializeMatrixColorFilterImpl(flexbuffers::Builder& fbb,
                                                              ColorFilter* colorFilter) {
  serializeColorFilterImpl(fbb, colorFilter);
  MatrixColorFilter* matrixColorFilter = static_cast<MatrixColorFilter*>(colorFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", 20, false, true);
  SerializeUtils::setFlexBufferMap(fbb, "AlphaIsUnchanged", matrixColorFilter->alphaIsUnchanged);
}
void ColorFilterSerialization::serializeModeColorFilterImpl(flexbuffers::Builder& fbb,
                                                            ColorFilter* colorFilter) {
  serializeColorFilterImpl(fbb, colorFilter);
  ModeColorFilter* modeColorFilter = static_cast<ModeColorFilter*>(colorFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Mode",
                                   SerializeUtils::blendModeToString(modeColorFilter->mode));
}
}  // namespace tgfx
#endif