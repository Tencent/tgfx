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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstdint>
#include <variant>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/shaders/Module.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class ProgramInfo;

/// Writes the analytic clip parameters (the slot's module fields) from the draw's IR analytic
/// term. The hairline families carry the same fields inside their geometry module, so the shared
/// body templates on the module that owns the fields.
template <const Module& module>
void WriteClipParams(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Rect = UniformElementIndexOf(module, "Rect");
  constexpr uint8_t HasClip = UniformElementIndexOf(module, "HasClip");
  constexpr uint8_t LocalRect = UniformElementIndexOf(module, "LocalRect");
  constexpr uint8_t RadiiX = UniformElementIndexOf(module, "RadiiX");
  constexpr uint8_t RadiiY = UniformElementIndexOf(module, "RadiiY");
  constexpr uint8_t AntiAlias = UniformElementIndexOf(module, "AntiAlias");
  constexpr uint8_t DeviceToLocal = UniformElementIndexOf(module, "DeviceToLocal");
  // The lowering already classified the clip into the IR's analytic term and copied every
  // parameter into its node chain (RectCoverage / RRectCoverage), so the writer reads the draw's
  // IR instead of searching the processor tree — the same values, sourced from the one place that
  // owns them.
  const AOTCoverageTerm* analytic = nullptr;
  const DrawIR* ir = info.getCarriedIR();
  if (ir != nullptr) {
    for (const auto& term : ir->coverage) {
      if (term.kind == AOTCoverageTermKind::Analytic) {
        analytic = &term;
        break;
      }
    }
  }
  if (analytic != nullptr) {
    for (size_t index = 0; index < analytic->graph.nodeCount(); ++index) {
      const auto* node =
          analytic->graph.nodeAt(static_cast<AOTNodeID>(static_cast<uint32_t>(index)));
      if (node == nullptr) {
        continue;
      }
      if (const auto* rect = std::get_if<AOTRectCoverageParameters>(&node->parameters)) {
        const Matrix deviceToLocal =
            Matrix::MakeAll(rect->deviceToLocal[0], rect->deviceToLocal[1], rect->deviceToLocal[2],
                            rect->deviceToLocal[3], rect->deviceToLocal[4], rect->deviceToLocal[5],
                            rect->deviceToLocal[6], rect->deviceToLocal[7], rect->deviceToLocal[8]);
        writer->set(LocalRect, std::array<float, 4>{rect->rect[0], rect->rect[1], rect->rect[2],
                                                    rect->rect[3]});
        // The RectCoverage node is the AA form: the lowering routes hard-edged rects through the
        // rrect node with a zero antiAlias.
        writer->set(AntiAlias, 1.0f);
        writer->set(DeviceToLocal, deviceToLocal);
        if (deviceToLocal == Matrix::I()) {
          // The device-space form uploads half a pixel outward so the coverage ramp centers on the
          // geometric boundary.
          writer->set(Rect, std::array<float, 4>{rect->rect[0] - 0.5f, rect->rect[1] - 0.5f,
                                                 rect->rect[2] + 0.5f, rect->rect[3] + 0.5f});
          writer->set(HasClip, static_cast<int32_t>(1));
        } else {
          writer->set(HasClip, static_cast<int32_t>(3));
        }
        return;
      }
      if (const auto* rrect = std::get_if<AOTRRectCoverageParameters>(&node->parameters)) {
        writer->set(LocalRect, std::array<float, 4>{rrect->rect[0], rrect->rect[1], rrect->rect[2],
                                                    rrect->rect[3]});
        writer->set(RadiiX, rrect->radiiX);
        writer->set(RadiiY, rrect->radiiY);
        writer->set(AntiAlias, rrect->antiAlias);
        writer->set(DeviceToLocal, Matrix::MakeAll(rrect->deviceToLocal[0], rrect->deviceToLocal[1],
                                                   rrect->deviceToLocal[2], rrect->deviceToLocal[3],
                                                   rrect->deviceToLocal[4], rrect->deviceToLocal[5],
                                                   rrect->deviceToLocal[6], rrect->deviceToLocal[7],
                                                   rrect->deviceToLocal[8]));
        writer->set(HasClip, static_cast<int32_t>(2));
        return;
      }
    }
  }
  // No clip: the contract branch reads nothing but the flag.
  writer->set(HasClip, static_cast<int32_t>(0));
}

}  // namespace tgfx

namespace tgfx {

struct Module;

/// The analytic clip module's writer (the runtime clip contract).
ModuleWriter ClipWriterOf(const Module& module);

}  // namespace tgfx
