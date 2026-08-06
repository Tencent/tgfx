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

#include "Blur1DFragmentProcessor.h"
#include <cstring>
#include "gpu/UniformData.h"

namespace tgfx {

Blur1DFragmentProcessor::Blur1DFragmentProcessor(uint32_t classID) : FragmentProcessor(classID) {
}

void Blur1DFragmentProcessor::setKernelData(UniformData* fragmentUniformData) const {
  // Pack the kernel into a vec4 array. Unused trailing slots stay zero and are never indexed
  // because the shader only accesses offsets within the stored radius.
  std::array<float, 4 * KERNEL_VEC4_COUNT> kernelData = {};
  const size_t weightCount =
      symmetric ? static_cast<size_t>(kernelRadius + 1) : static_cast<size_t>(2 * kernelRadius + 1);
  memcpy(kernelData.data(), kernel.data(), weightCount * sizeof(float));
  fragmentUniformData->setData("Kernel", kernelData);
  fragmentUniformData->setData("Radius", kernelRadius);
}

void Blur1DFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(static_cast<uint32_t>(symmetric));
}
}  // namespace tgfx
