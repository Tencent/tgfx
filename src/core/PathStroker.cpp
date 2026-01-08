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

#include "tgfx/core/PathStroker.h"
#include "core/AdaptiveDashEffect.h"
#include "core/PathRef.h"
#include "core/utils/StrokeUtils.h"
#include "include/core/SkStrokeParams.h"

namespace tgfx {

bool PathStroker::StrokePathWithMultiParams(Path* path, float width,
                                            const std::vector<PointParam>& params,
                                            float resolutionScale) {
  if (path == nullptr || params.empty()) {
    return false;
  }

  std::vector<pk::SkStrokeParams> skParams = {};
  skParams.reserve(params.size());
  for (const auto& param : params) {
    skParams.emplace_back(param.miterLimit, ToSkLineCap(param.cap), ToSkLineJoin(param.join));
  }

  auto& skPath = PathRef::WriteAccess(*path);
  pk::SkPath result = {};
  if (!pk::StrokePathWithMultiParams(skPath, &result, width, skParams, resolutionScale)) {
    return false;
  }
  skPath = result;
  return true;
}

bool PathStroker::StrokeDashPathWithMultiParams(Path* path, float width,
                                                const std::vector<PointParam>& params,
                                                const PointParam& defaultParam,
                                                const float intervals[], int count, float phase,
                                                float resolutionScale) {
  if (path == nullptr || intervals == nullptr || count <= 0) {
    return false;
  }

  // Create AdaptiveDashEffect to process the path
  AdaptiveDashEffect dashEffect(intervals, count, phase);

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
