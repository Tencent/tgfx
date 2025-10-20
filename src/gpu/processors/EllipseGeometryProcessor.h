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

namespace tgfx {
/**
 * Skia's sharing：
 * https://www.essentialmath.com/GDC2015/VanVerth_Jim_DrawingAntialiasedEllipse.pdf
 *
 * There is a formula that calculates the approximate distance from the point to the ellipse,
 * and the proof of the formula can be found in the link below.
 * https://www.researchgate.net/publication/222440289_Sampson_PD_Fitting_conic_sections_to_very_scattered_data_An_iterative_refinement_of_the_Bookstein_algorithm_Comput_Graphics_Image_Process_18_97-108
 */
class EllipseGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<EllipseGeometryProcessor> Make(
      BlockBuffer* buffer, int width, int height, bool stroke, bool useScale,
      std::optional<Color> commonColor, std::shared_ptr<ColorSpace> colorSpace = nullptr);

  std::string name() const override {
    return "EllipseGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  EllipseGeometryProcessor(int width, int height, bool stroke, bool useScale,
                           std::optional<Color> commonColor,
                           std::shared_ptr<ColorSpace> colorSpace);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute inPosition;
  Attribute inColor;
  Attribute inEllipseOffset;
  Attribute inEllipseRadii;

  int width = 1;
  int height = 1;
  bool stroke;
  bool useScale;
  std::optional<Color> commonColor = std::nullopt;
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;
};
}  // namespace tgfx
