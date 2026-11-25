/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/PrecompiledDiagnostics.h"
#include <iomanip>
#include <sstream>
#include "core/utils/Log.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

namespace {

void AppendFragmentProcessor(std::stringstream& stream, const FragmentProcessor* processor) {
  stream << processor->name();
  if (processor->numChildProcessors() == 0) {
    return;
  }
  stream << "(";
  for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, processor->childProcessor(i));
  }
  stream << ")";
}



}  // namespace

std::string BuildEffectSignature(const GeometryLayout& geometry,
                                 const std::vector<const FragmentProcessor*>& colorProcessors,
                                 const std::vector<const FragmentProcessor*>& coverageProcessors,
                                 const XferFacts& xfer) {
  std::stringstream stream;
  stream << "GP=" << GeometryKindName(geometry.parameters.kind) << ";ColorFP=[";
  for (size_t i = 0; i < colorProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, colorProcessors[i]);
  }
  stream << "];CoverageFP=[";
  for (size_t i = 0; i < coverageProcessors.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    AppendFragmentProcessor(stream, coverageProcessors[i]);
  }
  stream << "];XP=" << (xfer.shaderBlend() ? "shader" : "fixed");
  return stream.str();
}




}  // namespace tgfx
