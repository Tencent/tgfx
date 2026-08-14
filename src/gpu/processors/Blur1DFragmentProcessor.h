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

#include <array>
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

class UniformData;

/**
 * Base class for one-dimensional convolution blur processors whose kernel weights are precomputed
 * on the CPU and uploaded as a uniform vec4 array. Subclasses generate a blur-specific kernel in
 * computeKernel() and provide the shader loop bound in kernelLoopUpperBound(). The kernel must be
 * symmetric: the half-kernel is stored in kernel[0..kernelRadius] and the shader indexes it by
 * abs(offset).
 */
class Blur1DFragmentProcessor : public FragmentProcessor {
 public:
  // The maximum single-sided kernel radius. The total sample count (2 * radius + 1) must not
  // exceed 2 * MAX_KERNEL_SIZE - 1.
  static constexpr int MAX_KERNEL_RADIUS = 64;
  // Table capacity sized for the half-kernel of a symmetric blur. Only the first kernelRadius + 1
  // entries are populated.
  static constexpr int MAX_KERNEL_SIZE = MAX_KERNEL_RADIUS + 1;
  // Number of vec4 uniforms needed to hold the half-kernel.
  static constexpr int KERNEL_VEC4_COUNT = (MAX_KERNEL_SIZE + 3) / 4;

 protected:
  explicit Blur1DFragmentProcessor(uint32_t classID);

  /**
   * Precomputes the blur-specific kernel weights into kernel[] and sets kernelRadius. The radius
   * must be clamped to MAX_KERNEL_RADIUS so that the sample count fits the kernel table.
   */
  virtual void computeKernel() = 0;

  // The compile-time upper bound of the shader sampling loop. It determines the generated shader
  // code, so subclasses must include the loop bound in the processor key.
  virtual int kernelLoopUpperBound() const = 0;

  /**
   * Uploads the packed kernel and radius uniforms. The half-kernel uploads kernelRadius + 1
   * weights.
   */
  void setKernelData(UniformData* fragmentUniformData) const;

  // The single-sided radius of the kernel.
  int kernelRadius = 0;
  // The normalized half-kernel weights. kernel[0] is the center weight; kernel[i] is the shared
  // weight of the samples at offsets +i and -i.
  std::array<float, MAX_KERNEL_SIZE> kernel = {};
};
}  // namespace tgfx
