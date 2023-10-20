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

#include "PathRef.h"
#include "tgfx/core/Path.h"

namespace tgfx {
using namespace pk;

// When tessellating curved paths into linear segments, this defines the maximum distance in
// screen space which a segment may deviate from the mathematically correct value. Above this
// value, the segment will be subdivided. This value was chosen to approximate the super sampling
// accuracy of the raster path (16 samples, or one quarter pixel).
static constexpr float DEFAULT_TOLERANCE = 0.25f;

const SkPath& PathRef::ReadAccess(const Path& path) {
  return path.pathRef->path;
}

SkPath& PathRef::WriteAccess(Path& path) {
  return path.writableRef()->path;
}

int PathRef::ToAATriangles(const Path& path, const Rect& clipBounds, std::vector<float>* vertices) {
  const auto& skPath = path.pathRef->path;
  return skPath.toAATriangles(DEFAULT_TOLERANCE, *reinterpret_cast<const pk::SkRect*>(&clipBounds),
                              vertices);
}
}  // namespace tgfx