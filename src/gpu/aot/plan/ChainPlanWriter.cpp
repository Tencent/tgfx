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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/plan/ChainPlanWriter.h"
#include <cstring>
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/Program.h"

namespace tgfx {
namespace {

// Reads one parameter's value from the typed slot field: no pool packing round trip, the member
// receives exactly the field the composition's slot carries.
bool ReadPlanParam(const PlanParam& param, const AOTProgram& program, float* out) {
  if (param.slot < 0 || static_cast<size_t>(param.slot) >= program.slots.size()) {
    return false;
  }
  const auto& slot = program.slots[static_cast<size_t>(param.slot)];
  const auto* name = param.param;
  switch (param.op) {
    case ChainISA::ColorMatrix: {
      float linear[16] = {};
      float translate[4] = {};
      PackAOTColorMatrix(slot.colorMatrix.matrix, linear, translate);
      if (std::strcmp(name, "linear") == 0) {
        std::memcpy(out, linear, sizeof(linear));
      } else {
        std::memcpy(out, translate, sizeof(translate));
      }
      return true;
    }
    case ChainISA::Luma:
      out[0] = slot.luma.kr;
      out[1] = slot.luma.kg;
      out[2] = slot.luma.kb;
      return true;
    case ChainISA::AlphaThreshold:
      out[0] = slot.alphaThreshold.threshold;
      return true;
    case ChainISA::ConstColor:
      std::memcpy(out, slot.constColor.color.data(), sizeof(float) * 4);
      return true;
    case ChainISA::AARectCoverage:
      std::memcpy(out, slot.rectCoverage.rect.data(), sizeof(float) * 4);
      return true;
    case ChainISA::LocalRectCoverage:
      if (std::strcmp(name, "rect") == 0) {
        std::memcpy(out, slot.localRectCoverage.rect.data(), sizeof(float) * 4);
      } else {
        // Row-major 9 into std140's three vec4 columns.
        const auto& m = slot.localRectCoverage.deviceToLocal;
        for (size_t column = 0; column < 3; ++column) {
          out[column * 4 + 0] = m[column];
          out[column * 4 + 1] = m[3 + column];
          out[column * 4 + 2] = m[6 + column];
          out[column * 4 + 3] = 0.0f;
        }
      }
      return true;
    case ChainISA::RRectCoverage:
      if (std::strcmp(name, "rect") == 0) {
        std::memcpy(out, slot.rrectCoverage.rect.data(), sizeof(float) * 4);
      } else if (std::strcmp(name, "radiiX") == 0) {
        std::memcpy(out, slot.rrectCoverage.radiiX.data(), sizeof(float) * 4);
      } else if (std::strcmp(name, "radiiY") == 0) {
        std::memcpy(out, slot.rrectCoverage.radiiY.data(), sizeof(float) * 4);
      } else if (std::strcmp(name, "antiAlias") == 0) {
        out[0] = slot.rrectCoverage.antiAlias;
      } else {
        const auto& m = slot.rrectCoverage.deviceToLocal;
        for (size_t column = 0; column < 3; ++column) {
          out[column * 4 + 0] = m[column];
          out[column * 4 + 1] = m[3 + column];
          out[column * 4 + 2] = m[6 + column];
          out[column * 4 + 3] = 0.0f;
        }
      }
      return true;
    default:
      return false;
  }
}

}  // namespace

ChainPlanWriter::ChainPlanWriter(Variant variant, ChainPlan plan, uint32_t prefixBytes)
    : ProgramWriter(std::move(variant)), plan(std::move(plan)), prefixBytes(prefixBytes) {
}

void ChainPlanWriter::writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                                    UniformData* fragmentUniformData) const {
  // The plan's named parameter block first: the base fills the variant prefix (and the vertex
  // block) afterwards, so both regions land in the one fragment buffer the upload carries.
  const auto* sources = info.getSources();
  if (sources != nullptr && sources->chain != nullptr && fragmentUniformData != nullptr) {
    auto* bytes = fragmentUniformData->writableBytes();
    if (bytes != nullptr) {
      for (const auto& param : plan.params) {
        float value[16] = {};
        if (ReadPlanParam(param, *sources->chain, value)) {
          std::memcpy(bytes + prefixBytes + param.offset, value, param.size);
        }
      }
    }
  }
  ProgramWriter::writeUniforms(info, vertexUniformData, fragmentUniformData);
}

}  // namespace tgfx
