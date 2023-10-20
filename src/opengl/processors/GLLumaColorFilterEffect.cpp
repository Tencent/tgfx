/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLLumaColorFilterEffect.h"

namespace tgfx {
std::unique_ptr<LumaColorFilterEffect> LumaColorFilterEffect::Make() {
  return std::make_unique<GLLumaColorFilterEffect>();
}

void GLLumaColorFilterEffect::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  fragBuilder->codeAppendf(
      "%s = vec4(0.0, 0.0, 0.0, clamp(dot(vec3(0.21260000000000001, 0.71519999999999995, 0.0722), "
      "%s.rgb), 0.0, 1.0));",
      args.outputColor.c_str(), args.inputColor.c_str());
}
}  // namespace tgfx
