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

#include "GlassRefractionFragmentProcessor.h"

namespace tgfx {

GlassRefractionFragmentProcessor::GlassRefractionFragmentProcessor(
    PlacementPtr<FragmentProcessor> source, PlacementPtr<FragmentProcessor> mask,
    const GlassRefractionParams& params)
    : FragmentProcessor(ClassID()), params(params) {
  registerChildProcessor(std::move(source));
  if (mask) {
    registerChildProcessor(std::move(mask));
  }
}

void GlassRefractionFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(static_cast<int>(params.shapeType));
  key->write(params.hasMask ? 1 : 0);
}

}  // namespace tgfx
