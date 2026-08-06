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
 * computeKernel() and provide the shader loop bound in kernelLoopUpperBound(). The kernel may be
 * symmetric (stored as a half-kernel indexed by abs(offset)) or asymmetric (stored as a full-kernel
 * indexed by offset + radius), controlled by the symmetric flag.
 */
class Blur1DFragmentProcessor : public FragmentProcessor {
 public:
  // The maximum single-sided kernel radius. The total sample count (2 * radius + 1) must not
  // exceed MAX_KERNEL_SIZE.
  static constexpr int MAX_KERNEL_RADIUS = 20;
  // Table capacity sized for the full kernel of an asymmetric blur. Symmetric blurs only populate
  // the first kernelRadius + 1 entries.
  static constexpr int MAX_KERNEL_SIZE = 2 * MAX_KERNEL_RADIUS + 1;
  // Number of vec4 uniforms needed to hold the full kernel.
  static constexpr int KERNEL_VEC4_COUNT = (MAX_KERNEL_SIZE + 3) / 4;

 protected:
  explicit Blur1DFragmentProcessor(uint32_t classID);

  /**
   * Returns the total number of samples covered by the kernel (2 * kernelRadius + 1). Subclasses
   * clamp kernelRadius so the sample count never exceeds the kernel table capacity.
   */
  int sampleCount() const {
    return 2 * kernelRadius + 1;
  }

  /**
   * Precomputes the blur-specific kernel weights into kernel[] and sets kernelRadius. The radius
   * must be clamped to MAX_KERNEL_RADIUS so that the sample count fits the kernel table.
   */
  virtual void computeKernel() = 0;

  // The compile-time upper bound of the shader sampling loop. Together with the symmetric flag it
  // determines the generated shader code, so subclasses must include both in the processor key.
  virtual int kernelLoopUpperBound() const = 0;

  /**
   * Uploads the packed kernel and radius uniforms. Symmetric kernels upload kernelRadius + 1
   * weights; asymmetric kernels upload 2 * kernelRadius + 1 weights.
   */
  void setKernelData(UniformData* fragmentUniformData) const;

  void onComputeProcessorKey(BytesKey* key) const override;

  // True when the kernel is symmetric, in which case the shader indexes the half-kernel by abs(i).
  bool symmetric = true;
  // The single-sided radius of the kernel.
  int kernelRadius = 0;
  // The kernel weights. Symmetric kernels store kernel[0..kernelRadius]; asymmetric kernels store
  // kernel[0..2 * kernelRadius] in offset order.
  std::array<float, MAX_KERNEL_SIZE> kernel = {};
};
}  // namespace tgfx
