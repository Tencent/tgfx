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

#include "TrimPathEffect.h"
#include <cmath>
#include "core/utils/MathExtra.h"
#include "tgfx/core/PathMeasure.h"

namespace tgfx {

struct Segment {
  float start;
  float end;
};

std::shared_ptr<PathEffect> PathEffect::MakeTrim(float start, float end) {
  if (std::isnan(start) || std::isnan(end)) {
    return nullptr;
  }
  // Full path coverage in forward direction, no trimming needed
  // Note: reversed full coverage (e.g., 1.0 to 0.0) still needs processing to reverse the path
  if (end - start >= 1.0f) {
    return nullptr;
  }
  return std::shared_ptr<PathEffect>(new TrimPathEffect(start, end));
}

bool TrimPathEffect::filterPath(Path* path) const {
  if (path == nullptr) {
    return false;
  }

  // Empty trim range, return empty path
  if (start == end) {
    path->reset();
    return true;
  }

  auto trimStart = start;
  auto trimEnd = end;

  // Handle reversed case (end < start): swap to make trimStart < trimEnd, mark for later reversal
  bool reversed = trimEnd < trimStart;
  if (reversed) {
    std::swap(trimStart, trimEnd);
  }

  // Normalize: shift to bring trimStart into [0, 1) range
  float shift = std::floor(trimStart);
  trimStart -= shift;
  trimEnd -= shift;

  // Determine if wrap-around is needed (trimEnd > 1.0 since trimStart is in [0, 1))
  bool wrapAround = trimEnd > 1.0f;

  auto fillType = path->getFillType();
  auto pathMeasure = PathMeasure::MakeFrom(*path);

  // Calculate total length and collect contour lengths
  float totalLength = 0;
  std::vector<float> contourLengths = {};
  do {
    auto length = pathMeasure->getLength();
    contourLengths.push_back(length);
    totalLength += length;
  } while (pathMeasure->nextContour());

  if (totalLength <= 0) {
    path->reset();
    return true;
  }

  // Build segments to extract
  std::vector<Segment> segments = {};

  if (wrapAround) {
    // [0...start...1...end] -> [start, 1] + [0, end-1]
    segments.push_back({trimStart * totalLength, totalLength});
    segments.push_back({0.0f, (trimEnd - 1.0f) * totalLength});
  } else {
    // [0...start...end...1] -> [start, end]
    segments.push_back({trimStart * totalLength, trimEnd * totalLength});
  }

  // Collect extracted paths per contour for potential reversal
  std::vector<Path> extractedPaths = {};
  pathMeasure = PathMeasure::MakeFrom(*path);
  float accumulatedLength = 0;

  for (auto contourLength : contourLengths) {
    if (contourLength <= 0) {
      pathMeasure->nextContour();
      continue;
    }

    float contourStart = accumulatedLength;
    float contourEnd = accumulatedLength + contourLength;

    if (wrapAround) {
      // Check which segments intersect this contour
      std::vector<Segment> localSegments = {};
      for (auto& segment : segments) {
        if (segment.start < contourEnd && segment.end > contourStart) {
          float localStart = std::max(0.f, segment.start - contourStart);
          float localEnd = std::min(contourLength, segment.end - contourStart);
          localSegments.push_back({localStart, localEnd});
        }
      }

      // Check if this contour needs seamless connection:
      // Both segments exist in this contour AND contour is closed
      bool needSeamlessConnection =
          localSegments.size() == 2 && pathMeasure->isClosed() &&
          segments[0].start >= contourStart && segments[0].end <= contourEnd &&
          segments[1].start >= contourStart && segments[1].end <= contourEnd;

      if (needSeamlessConnection) {
        // Both segments are within this closed contour, connect seamlessly
        Path firstSegment = {};
        Path secondSegment = {};
        pathMeasure->getSegment(localSegments[0].start, localSegments[0].end, &firstSegment);
        pathMeasure->getSegment(localSegments[1].start, localSegments[1].end, &secondSegment);
        firstSegment.addPath(secondSegment, PathOp::Extend);
        extractedPaths.push_back(std::move(firstSegment));
      } else {
        // Handle segments separately
        for (auto& localSegment : localSegments) {
          Path segmentPath = {};
          pathMeasure->getSegment(localSegment.start, localSegment.end, &segmentPath);
          extractedPaths.push_back(std::move(segmentPath));
        }
      }
    } else {
      // Normal: extract single segment
      auto& segment = segments[0];
      if (segment.start < contourEnd && segment.end > contourStart) {
        float localStart = std::max(0.f, segment.start - contourStart);
        float localEnd = std::min(contourLength, segment.end - contourStart);
        Path segmentPath = {};
        pathMeasure->getSegment(localStart, localEnd, &segmentPath);
        // If extracting entire closed contour, preserve the closed state
        if (FloatNearlyZero(localStart) && FloatNearlyEqual(localEnd, contourLength) &&
            pathMeasure->isClosed()) {
          segmentPath.close();
        }
        extractedPaths.push_back(std::move(segmentPath));
      }
    }

    accumulatedLength = contourEnd;
    pathMeasure->nextContour();
  }

  // Build final path
  Path tempPath = {};
  if (reversed) {
    // Reverse each segment and add in reverse order
    for (auto it = extractedPaths.rbegin(); it != extractedPaths.rend(); ++it) {
      it->reverse();
      tempPath.addPath(*it);
    }
  } else {
    for (auto& extractedPath : extractedPaths) {
      tempPath.addPath(extractedPath);
    }
  }

  tempPath.setFillType(fillType);
  *path = std::move(tempPath);
  return true;
}

}  // namespace tgfx
