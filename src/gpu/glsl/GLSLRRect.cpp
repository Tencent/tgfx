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

#include "GLSLRRect.h"

namespace tgfx {
void EmitRRectSDFCoverage(FragmentShaderBuilder* fragBuilder, const char* centeredPos, bool stroke,
                          const char* strokeWidthFsIn) {
  // Calculate outer round rect coverage using SDF
  fragBuilder->codeAppendf("vec2 q = abs(%s) - halfSize + radii;", centeredPos);
  fragBuilder->codeAppend(
      "float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) + length(max(q / radii, 0.0)) - 1.0;");
  fragBuilder->codeAppend("float outerCoverage = step(d, 0.0);");

  if (stroke) {
    // Stroke mode: also check inner round rect using SDF
    fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthFsIn);
    fragBuilder->codeAppend("vec2 innerHalfSize = halfSize - 2.0 * sw;");
    fragBuilder->codeAppend("vec2 innerRadii = max(radii - 2.0 * sw, vec2(0.0));");
    fragBuilder->codeAppend("float innerCoverage = 0.0;");
    // Check if inner rect is valid (not degenerate)
    fragBuilder->codeAppend("if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {");
    fragBuilder->codeAppendf("  vec2 qi = abs(%s) - innerHalfSize + innerRadii;", centeredPos);
    // Use safe division for inner radii (avoid division by zero)
    fragBuilder->codeAppend("  vec2 safeInnerRadii = max(innerRadii, vec2(0.001));");
    fragBuilder->codeAppend(
        "  float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) + "
        "length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;");
    fragBuilder->codeAppend("  innerCoverage = step(di, 0.0);");
    fragBuilder->codeAppend("}");

    // Final coverage: inside outer but outside inner
    fragBuilder->codeAppend("float coverage = outerCoverage * (1.0 - innerCoverage);");
  } else {
    // Fill mode: just use outer coverage
    fragBuilder->codeAppend("float coverage = outerCoverage;");
  }
}
}  // namespace tgfx
