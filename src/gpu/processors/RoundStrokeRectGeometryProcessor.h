/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class RoundStrokeRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<RoundStrokeRectGeometryProcessor> Make(BlockBuffer* buffer, AAType aaType,
                                                             std::optional<Color> commonColor,
                                                             std::optional<Matrix> uvMatrix);

  std::string name() const override {
    return "RoundStrokeRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID
  RoundStrokeRectGeometryProcessor(AAType aa, std::optional<Color> commonColor,
                                   std::optional<Matrix> uvMatrix);
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute inPosition;
  Attribute inCoverage;
  Attribute inEllipseOffset;
  Attribute inEllipseRadii;
  Attribute inUVCoord;
  Attribute inColor;

  AAType aaType = AAType::None;
  std::optional<Color> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
};
}  // namespace tgfx
