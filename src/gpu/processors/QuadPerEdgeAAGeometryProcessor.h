/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Paint.h"

namespace tgfx {
class QuadPerEdgeAAGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<QuadPerEdgeAAGeometryProcessor> Make(BlockBuffer* buffer, int width,
                                                           int height, AAType aa,
                                                           std::optional<Color> uniformColor,
                                                           bool useUVCoord);

  std::string name() const override {
    return "QuadPerEdgeAAGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  QuadPerEdgeAAGeometryProcessor(int width, int height, AAType aa,
                                 std::optional<Color> uniformColor, bool useUVCoord);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position;  // May contain coverage as last channel
  Attribute uvCoord;
  Attribute color;

  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  std::optional<Color> uniformColor = std::nullopt;
  bool useUVCoord = false;
};
}  // namespace tgfx
