/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AdaptiveDashEffect.h"
#include "PathRef.h"
#include "PathSegmentMeasure.h"

namespace tgfx {

inline bool IsEven(int x) {
  return !(x & 1);
}

std::shared_ptr<PathEffect> PathEffect::MakeAdaptiveDash(const float intervals[], int count,
                                                         float phase) {
  return std::make_shared<AdaptiveDashEffect>(intervals, count, phase);
}

static float AdjusetPhase(float intervalLength, float phase) {
  if (phase < 0) {
    phase = -phase;
    if (phase > intervalLength) {
      phase = PkScalarMod(phase, intervalLength);
    }
    phase = intervalLength - phase;
    if (phase == intervalLength) {
      phase = 0;
    }
  } else if (phase >= intervalLength) {
    phase = PkScalarMod(phase, intervalLength);
  }
  return phase;
}

AdaptiveDashEffect::AdaptiveDashEffect(const float intervals[], int count, float phase)
    : _intervals(intervals, intervals + count) {
  intervalLength = 0;
  for (float interval : _intervals) {
    intervalLength += interval;
  }
  _phase = AdjusetPhase(intervalLength, phase);
}

bool AdaptiveDashEffect::filterPath(Path* path) const {
  if (path == nullptr) {
    return false;
  }
  if (intervalLength == 0) {
    path->reset();
    return true;
  }

  auto meas = PathSegmentMeasure::MakeFrom(*path);
  Path dst = {};
  auto count = static_cast<int>(_intervals.size());
  float dashCount = 0;
  do {
    auto skipFirstSegment = meas->isClosed();
    bool needGetFirstSegment = false;
    float initialDashLength = 0;
    bool needMoveTo = true;
    do {
      auto length = meas->getSegmentLength();
      auto ratio = length / intervalLength;
      ratio = std::max(std::roundf(ratio), 1.0f);

      dashCount += ratio * (count >> 1);
      if (dashCount > kMaxDashCount) {
        return false;
      }

      auto scale = length / (ratio * intervalLength);
      auto currentPos = -_phase * scale;
      int dashIndex = 0;

      while (currentPos < length) {
        float dashLen = _intervals[static_cast<size_t>(dashIndex)] * scale;
        if (IsEven(dashIndex) && currentPos + dashLen > 0) {
          if (currentPos < 0 && skipFirstSegment) {
            // skip first segment and we need to apply at the end
            needGetFirstSegment = true;
            initialDashLength = dashLen + currentPos;
          } else {
            meas->getSegment(currentPos, currentPos + dashLen, needMoveTo, &dst);
          }
        }
        currentPos += dashLen;
        if (dashIndex == count - 1) {
          dashIndex = 0;
        } else {
          dashIndex++;
        }
        needMoveTo = true;
      }
      needMoveTo = IsEven(dashIndex);
      skipFirstSegment = false;
    } while (meas->nextSegment());
    if (needGetFirstSegment) {
      meas->resetSegement();
      meas->getSegment(0, initialDashLength, false, &dst);
    }
  } while (meas->nextContour());

  *path = std::move(dst);
  return true;
}

}  // namespace tgfx
