/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "layers/layerstyles/GlassUDF.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

class GlassRefractionImageFilter : public ImageFilter {
 public:
  /**
   * @param maskRequest Describes the refraction UDF field to generate at playback time. Empty on
   * the analytical SDF path, which needs no field.
   * @param edgeMaskRequest Describes the edge light UDF field. Empty when the edge light is off or
   * on the analytical SDF path.
   */
  GlassRefractionImageFilter(const GlassRefractionParams& params,
                             const GlassSDFGeometryParams& sdfParams,
                             const GlassUDFGeometryParams& udfParams,
                             const GlassUDFRequest& maskRequest = {},
                             const GlassUDFRequest& edgeMaskRequest = {});

 protected:
  Type type() const override {
    return Type::Runtime;
  }

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

  /**
   * Outsets the bounds by the maximum refraction displacement (plus the dispersion channel
   * spread). FilterImage's direct-attach branch requires the filter's output bounds to contain
   * the draw bounds; with the outset it always does, so the refraction shader is evaluated at
   * draw (screen) resolution instead of being baked into an offscreen texture.
   */
  Rect onFilterBounds(const Rect& rect, MapDirection mapDirection) const override;

 private:
  GlassRefractionParams params;
  GlassSDFGeometryParams sdfParams;
  GlassUDFGeometryParams udfParams;
  GlassUDFRequest maskRequest;
  GlassUDFRequest edgeMaskRequest;
};

}  // namespace tgfx
