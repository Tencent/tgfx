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

#include "ShaderMaskFilter.h"
#include "core/utils/Types.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<MaskFilter> MaskFilter::MakeShader(std::shared_ptr<Shader> shader, bool inverted) {
  if (shader == nullptr) {
    return nullptr;
  }
  return std::make_shared<ShaderMaskFilter>(std::move(shader), inverted);
}

std::shared_ptr<MaskFilter> ShaderMaskFilter::makeWithMatrix(const Matrix& viewMatrix) const {
  auto newShader = shader->makeWithMatrix(viewMatrix);
  return MakeShader(std::move(newShader), inverted);
}

bool ShaderMaskFilter::isEqual(const MaskFilter* maskFilter) const {
  auto type = Types::Get(maskFilter);
  if (type != Types::MaskFilterType::Shader) {
    return false;
  }
  auto other = static_cast<const ShaderMaskFilter*>(maskFilter);
  return inverted == other->inverted && shader->isEqual(other->shader.get());
}

PlacementPtr<FragmentProcessor> ShaderMaskFilter::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto processor = FragmentProcessor::Make(shader, args, uvMatrix);
  auto allocator = args.context->drawingAllocator();
  if (processor == nullptr && inverted) {
    return ConstColorProcessor::Make(allocator, {}, InputMode::Ignore);
  }
  return FragmentProcessor::MulInputByChildAlpha(allocator, std::move(processor), inverted);
}
}  // namespace tgfx
