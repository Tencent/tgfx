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

#include "tgfx/core/PathMeasure.h"
#include "core/PathRef.h"

namespace tgfx {
using namespace pk;

class PkPathMeasure : public PathMeasure {
 public:
  explicit PkPathMeasure(const SkPath& path) {
    pathMeasure = new SkPathMeasure(path, false);
  }

  ~PkPathMeasure() override {
    delete pathMeasure;
  }

  float getLength() override {
    return pathMeasure->getLength();
  }

  bool getSegment(float startD, float stopD, Path* result) override {
    if (result == nullptr) {
      return false;
    }
    auto& path = PathRef::WriteAccess(*result);
    return pathMeasure->getSegment(startD, stopD, &path, true);
  }

  bool getPosTan(float distance, Point* position, Point* tangent) override {
    if (position == nullptr || tangent == nullptr) {
      return false;
    }

    SkPoint point{};
    SkVector tan{};

    auto ret = pathMeasure->getPosTan(distance, &point, &tan);
    position->set(point.x(), point.y());
    tangent->set(tan.x(), tan.y());
    return ret;
  }

  bool isClosed() override {
    return pathMeasure->isClosed();
  }

  bool nextContour() override {
    return pathMeasure->nextContour();
  }

 private:
  SkPathMeasure* pathMeasure = nullptr;
};

std::unique_ptr<PathMeasure> PathMeasure::MakeFrom(const Path& path) {
  return std::make_unique<PkPathMeasure>(PathRef::ReadAccess(path));
}
}  // namespace tgfx
