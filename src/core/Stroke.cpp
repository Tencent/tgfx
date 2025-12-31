/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/Stroke.h"
#include "AdaptiveDashEffect.h"
#include "core/PathRef.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "include/core/SkStrokeParams.h"
#include "tgfx/core/PathStroker.h"

namespace tgfx {
using namespace pk;

static SkPaint::Cap ToSkLineCap(LineCap cap) {
  switch (cap) {
    case LineCap::Round:
      return SkPaint::kRound_Cap;
    case LineCap::Square:
      return SkPaint::kSquare_Cap;
    default:
      return SkPaint::kButt_Cap;
  }
}

static SkPaint::Join ToSkLineJoin(LineJoin join) {
  switch (join) {
    case LineJoin::Round:
      return SkPaint::kRound_Join;
    case LineJoin::Bevel:
      return SkPaint::kBevel_Join;
    default:
      return SkPaint::kMiter_Join;
  }
}

bool Stroke::applyToPath(Path* path, float resolutionScale) const {
  if (path == nullptr) {
    return false;
  }
  if (IsHairlineStroke(*this)) {
    return true;
  }
  SkPaint paint = {};
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(width);
  paint.setStrokeCap(ToSkLineCap(cap));
  paint.setStrokeJoin(ToSkLineJoin(join));
  paint.setStrokeMiter(miterLimit);
  auto& skPath = PathRef::WriteAccess(*path);
  return paint.getFillPath(skPath, &skPath, nullptr, resolutionScale);
}

bool PathStroker::StrokePathWithMultiParams(Path* path, float width,
                                            const std::vector<PointParam>& params,
                                            float resolutionScale) {
  if (path == nullptr || params.empty()) {
    return false;
  }

  std::vector<SkStrokeParams> skParams = {};
  skParams.reserve(params.size());
  for (const auto& param : params) {
    skParams.emplace_back(param.miterLimit, ToSkLineCap(param.cap), ToSkLineJoin(param.join));
  }

  auto& skPath = PathRef::WriteAccess(*path);
  SkPath result = {};
  if (!pk::StrokePathWithMultiParams(skPath, &result, width, skParams, resolutionScale)) {
    return false;
  }
  skPath = result;
  return true;
}

bool PathStroker::StrokeDashPathWithMultiParams(Path* path, float width,
                                                const std::vector<PointParam>& params,
                                                const PointParam& defaultParam,
                                                const std::vector<float>& intervals, int count,
                                                float phase, float resolutionScale) {
  if (path == nullptr || intervals.empty() || count <= 0) {
    return false;
  }

  // Create AdaptiveDashEffect to process the path
  AdaptiveDashEffect dashEffect(intervals.data(), count, phase);

  // Apply dash effect and get parameter mapping
  AdaptiveDashEffect::PointParamMapping outputMapping = {};
  outputMapping.defaultParam = defaultParam;
  if (!dashEffect.onFilterPath(path, &params, &outputMapping)) {
    return false;
  }

  // Apply stroke with the mapped parameters
  return StrokePathWithMultiParams(path, width, outputMapping.vertexParams, resolutionScale);
}
}  // namespace tgfx
