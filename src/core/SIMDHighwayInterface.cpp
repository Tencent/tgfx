/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#ifdef _MSC_VER
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/SIMDHighwayInterface.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
void TransPointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto* fdst = reinterpret_cast<float*>(&dst[0]);
    const auto* fsrc = reinterpret_cast<const float*>(&src[0]);
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    std::size_t size = static_cast<size_t>(count * 2);
    std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto res = hn::Add(srcSimd, transSimd);
      hn::StoreU(res, d, &fdst[i]);
    }
    for (std::size_t i = vecSize; i < size; i++) {
      fdst[i] = fsrc[i] + (i % 2 == 0 ? tx : ty);
    }
  }
}

void ScalePointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto* fdst = reinterpret_cast<float*>(&dst[0]);
    const auto* fsrc = reinterpret_cast<const float*>(&src[0]);
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    float sx = m.getScaleX();
    float sy = m.getScaleY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto sxVec = hn::Set(d, sx);
    auto syVec = hn::Set(d, sy);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    auto scaleSimd = hn::InterleaveLower(sxVec, syVec);
    std::size_t size = static_cast<size_t>(count * 2);
    std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto res = hn::MulAdd(srcSimd, scaleSimd, transSimd);
      hn::StoreU(res, d, &fdst[i]);
    }
    for (std::size_t i = vecSize; i < size; i++) {
      fdst[i] = fsrc[i] * (i % 2 == 0 ? sx : sy) + (i % 2 == 0 ? tx : ty);
    }
  }
}

void AffinePointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto* fdst = reinterpret_cast<float*>(&dst[0]);
    const auto* fsrc = reinterpret_cast<const float*>(&src[0]);
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    float sx = m.getScaleX();
    float sy = m.getScaleY();
    float kx = m.getSkewX();
    float ky = m.getSkewY();
    const HWY_FULL(float) d;
    auto txVec = hn::Set(d, tx);
    auto tyVec = hn::Set(d, ty);
    auto sxVec = hn::Set(d, sx);
    auto syVec = hn::Set(d, sy);
    auto kxVec = hn::Set(d, kx);
    auto kyVec = hn::Set(d, ky);
    auto transSimd = hn::InterleaveLower(txVec, tyVec);
    auto scaleSimd = hn::InterleaveLower(sxVec, syVec);
    auto skewSimd = hn::InterleaveLower(kxVec, kyVec);
    std::size_t size = static_cast<size_t>(count * 2);
    std::size_t vecSize = size - size % hn::Lanes(d);
    for (std::size_t i = 0; i < vecSize; i += hn::Lanes(d)) {
      auto srcSimd = hn::LoadU(d, &fsrc[i]);
      auto swzSimd = hn::Reverse2(d, srcSimd);
      auto res = hn::MulAdd(srcSimd, scaleSimd, hn::MulAdd(swzSimd, skewSimd, transSimd));
      hn::StoreU(res, d, &fdst[i]);
    }
    float temp = 0.0f;
    for (std::size_t i = vecSize; i < size; i++) {
      float swz = 0.0f;
      size_t swzindex = (i % 2 == 0 ? i + 1 : i - 1);
      if (fdst == fsrc) {
        temp = fsrc[i];
        swz = swzindex > i ? fsrc[swzindex] : temp;
      } else {
        swz = fsrc[swzindex];
      }
      fdst[i] =
          fsrc[i] * (i % 2 == 0 ? sx : sy) + (i % 2 == 0 ? tx : ty) + swz * (i % 2 == 0 ? kx : ky);
    }
  }
}

void MapRectHWYImpl(const Matrix& m, Rect* dst, const Rect& src) {
  if (m.getType() <= Matrix::TranslateMask) {
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    const hn::Full128<float> d;
    auto trans = hn::Dup128VecFromValues(d, tx, ty, tx, ty);
    auto ltrb = hn::Add(hn::LoadU(d, &src.left), trans);
    auto rblt = hn::Reverse2(d, hn::Reverse(d, ltrb));
    auto min = hn::Min(ltrb, rblt);
    auto max = hn::Max(ltrb, rblt);
    auto res = hn::ConcatUpperLower(d, max, min);
    hn::StoreU(res, d, &dst->left);
    return;
  }
  if (m.isScaleTranslate()) {
    float sx = m.getScaleX();
    float sy = m.getScaleY();
    float tx = m.getTranslateX();
    float ty = m.getTranslateY();
    const hn::Full128<float> d;
    auto scale = hn::Dup128VecFromValues(d, sx, sy, sx, sy);
    auto trans = hn::Dup128VecFromValues(d, tx, ty, tx, ty);
    auto ltrb = hn::MulAdd(hn::LoadU(d, &src.left), scale, trans);
    auto rblt = hn::Reverse2(d, hn::Reverse(d, ltrb));
    auto min = hn::Min(ltrb, rblt);
    auto max = hn::Max(ltrb, rblt);
    auto res = hn::ConcatUpperLower(d, max, min);
    hn::StoreU(res, d, &dst->left);
  } else {
    Point quad[4];
    quad[0].set(src.left, src.top);
    quad[1].set(src.right, src.top);
    quad[2].set(src.right, src.bottom);
    quad[3].set(src.left, src.bottom);
    m.mapPoints(quad, quad, 4);
    dst->setBounds(quad, 4);
  }
}

bool SetBoundsHWYImpl(Rect* rect, const Point* pts, int count) {
  if (count <= 0) {
    rect->setEmpty();
    return false;
  }
  const hn::Full128<float> d;
  hn::Vec<hn::Full128<float>> min, max;
  if (count & 1) {
    min = max = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[0].x, pts[0].y);
    pts += 1;
    count -= 1;
  } else {
    min = max = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    pts += 2;
    count -= 2;
  }
  auto accum = hn::Mul(min, hn::Set(d, 0.0f));
  while (count) {
    auto xy = hn::Dup128VecFromValues(d, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
    accum = hn::Mul(accum, xy);
    min = hn::Min(min, xy);
    max = hn::Max(max, xy);
    pts += 2;
    count -= 2;
  }
  auto mask = hn::Eq(hn::Mul(accum, hn::Set(d, 0.0f)), Set(d, 0.0f));
  hn::DFromM<decltype(mask)> md;
  const bool allFinite = hn::AllTrue(md, mask);
  if (allFinite) {
    float minArray[4] = {};
    float maxArray[4] = {};
    hn::Store(min, d, minArray);
    hn::Store(max, d, maxArray);
    rect->setLTRB(std::min(minArray[0], minArray[2]), std::min(minArray[1], minArray[3]),
                  std::max(maxArray[0], maxArray[2]), std::max(maxArray[1], maxArray[3]));
    return true;
  } else {
    rect->setEmpty();
    return false;
  }
}
}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(TransPointsHWYImpl);
HWY_EXPORT(ScalePointsHWYImpl);
HWY_EXPORT(AffinePointsHWYImpl);
HWY_EXPORT(MapRectHWYImpl);
HWY_EXPORT(SetBoundsHWYImpl);

const Matrix::MapPtsProc Matrix::MapPtsProcs[] = {
  Matrix::IdentityPoints, HWY_DYNAMIC_DISPATCH(TransPointsHWYImpl), HWY_DYNAMIC_DISPATCH(ScalePointsHWYImpl), HWY_DYNAMIC_DISPATCH(ScalePointsHWYImpl),
  HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl), HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl), HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl), HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl)
};

void Matrix::mapRect(Rect* dst, const Rect& src) const {
  return HWY_DYNAMIC_DISPATCH(MapRectHWYImpl)(*this, dst, src);
}

bool Rect::setBounds(const Point pts[], int count) {
  return HWY_DYNAMIC_DISPATCH(SetBoundsHWYImpl)(this, pts, count);
}
}  // namespace tgfx
#endif

#endif