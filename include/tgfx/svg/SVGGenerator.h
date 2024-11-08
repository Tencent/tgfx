/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <sstream>
#include "tgfx/core/Canvas.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class SVGContext;

class SVGGenerator {
 public:
  SVGGenerator() = default;
  ~SVGGenerator();

  Canvas* beginGenerate(Context* GPUContext, const ISize& size);
  Canvas* getCanvas() const;
  std::string finishGenerate();

 private:
  bool _actively = false;
  SVGContext* _svgContext = nullptr;
  Canvas* _canvas = nullptr;
  std::stringstream _svgStream;
};

}  // namespace tgfx