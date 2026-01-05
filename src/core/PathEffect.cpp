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

#include "tgfx/core/PathEffect.h"
#include "AdaptiveDashEffect.h"
#include "core/PathRef.h"

namespace tgfx {
using namespace pk;

class SkPathEffectWrapper : public PathEffect {
 public:
  explicit SkPathEffectWrapper(sk_sp<SkPathEffect> effect) : pathEffect(std::move(effect)) {
  }

  bool filterPath(Path* path) const override {
    if (path == nullptr) {
      return false;
    }
    auto& skPath = PathRef::WriteAccess(*path);
    SkStrokeRec rec(SkStrokeRec::kHairline_InitStyle);
    return pathEffect->filterPath(&skPath, skPath, &rec, nullptr);
  }

 private:
  sk_sp<SkPathEffect> pathEffect = nullptr;
};

std::shared_ptr<PathEffect> PathEffect::MakeDash(const float* intervals, int count, float phase,
                                                 bool adaptive) {
  if (adaptive) {
    return std::make_shared<AdaptiveDashEffect>(intervals, count, phase);
  }
  auto effect = SkDashPathEffect::Make(intervals, count, phase);
  if (effect == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<PathEffect>(new SkPathEffectWrapper(std::move(effect)));
}

std::shared_ptr<PathEffect> PathEffect::MakeCorner(float radius) {
  auto effect = SkCornerPathEffect::Make(radius);
  if (effect == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<PathEffect>(new SkPathEffectWrapper(std::move(effect)));
}
}  // namespace tgfx
