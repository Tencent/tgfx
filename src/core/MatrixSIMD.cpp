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

#include "tgfx/core/Matrix.h"
// First undef to prevent error when re-included.
#undef HWY_TARGET_INCLUDE
// For dynamic dispatch, specify the name of the current file (unfortunately
// __FILE__ is not reliable) so that foreach_target.h can re-include it.
#define HWY_TARGET_INCLUDE "core/MatrixSIMD.cpp"
// Generates code for each enabled target by re-including this source file.
#include "hwy/foreach_target.h"  // IWYU pragma: keep

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
// Must come after foreach_target.h to avoid redefinition errors.
#include "hwy/highway.h"
#pragma clang diagnostic pop

HWY_BEFORE_NAMESPACE();
namespace tgfx {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
void TransPointsHWYImpl(const Matrix& m, Point* dst, const Point* src, int count) {
  if (count > 0) {
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
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
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
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
    auto fdst = reinterpret_cast<float*>(&dst[0]);
    const auto fsrc = reinterpret_cast<const float*>(&src[0]);
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
        swz = swzindex > i ? fsrc[swzindex] : temp;
        temp = fsrc[i];
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
}  // namespace HWY_NAMESPACE
}  // namespace tgfx
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace tgfx {
HWY_EXPORT(TransPointsHWYImpl);
HWY_EXPORT(ScalePointsHWYImpl);
HWY_EXPORT(AffinePointsHWYImpl);
HWY_EXPORT(MapRectHWYImpl);
void Matrix::TransPoints(const Matrix& m, Point* dst, const Point* src, int count) {
  return HWY_DYNAMIC_DISPATCH(TransPointsHWYImpl)(m, dst, src, count);
}

void Matrix::ScalePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  return HWY_DYNAMIC_DISPATCH(ScalePointsHWYImpl)(m, dst, src, count);
}

void Matrix::AffinePoints(const Matrix& m, Point dst[], const Point src[], int count) {
  return HWY_DYNAMIC_DISPATCH(AffinePointsHWYImpl)(m, dst, src, count);
}

void Matrix::mapRect(Rect* dst, const Rect& src) const {
  return HWY_DYNAMIC_DISPATCH(MapRectHWYImpl)(*this, dst, src);
}
}  // namespace tgfx
#endif
