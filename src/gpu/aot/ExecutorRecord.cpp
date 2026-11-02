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

#include "gpu/aot/ExecutorRecord.h"
#include <string_view>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/kernels/MeshFillShader.h"
#include "gpu/shaders/kernels/QuadColorFillShader.h"
#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include "gpu/shaders/kernels/SolidColorFillShader.h"

namespace tgfx {
namespace {

constexpr uint32_t GeometryBit(AOTGeometryKind kind) {
  return 1u << static_cast<uint32_t>(kind);
}

constexpr uint8_t CoverageBit(CoverageKind kind) {
  return static_cast<uint8_t>(1u << static_cast<uint32_t>(kind));
}

// QuadTextureFillShader: the quad-edge-AA geometry, one bare texture leaf as the color, any of
// the four coverage classifications (the local mask raises HAS_LOCAL_MASK, the others ride
// runtime uniforms), no GP subset over a projective transform, and the uv-coord axis from the
// missing uv matrix.
constexpr AOTExecutorRecord kQuadTextureRecord = {
    QuadTextureFillShader::Name,
    GeometryBit(AOTGeometryKind::QuadPerEdgeAA),
    /*servesAlphaOnly=*/true,
    AOTRecordColorShape::BareTexture,
    static_cast<uint8_t>(CoverageBit(CoverageKind::AARect) | CoverageBit(CoverageKind::RRect) |
                         CoverageBit(CoverageKind::DeviceMask) |
                         CoverageBit(CoverageKind::LocalMask)),
    /*coverageRequiresColorFromPaint=*/false,
    /*localMaskRaisesAxis=*/true,
    /*deviceMaskRaisesAxis=*/false,
    /*rejectsPerspectiveSubset=*/true,
    /*uvCoordFromMissingMatrix=*/true,
};

// SolidColorFillShader: the default geometry fill whose color is the GP's Color uniform. The only
// fragment processor it takes is one direct analytic clip, and only when the color came from the
// paint (an explicit const-color processor describes a different expression).
constexpr AOTExecutorRecord kSolidColorRecord = {
    SolidColorFillShader::Name,
    GeometryBit(AOTGeometryKind::Default),
    /*servesAlphaOnly=*/true,
    AOTRecordColorShape::None,
    static_cast<uint8_t>(CoverageBit(CoverageKind::AARect) | CoverageBit(CoverageKind::RRect)),
    /*coverageRequiresColorFromPaint=*/true,
    /*localMaskRaisesAxis=*/false,
    /*deviceMaskRaisesAxis=*/false,
    /*rejectsPerspectiveSubset=*/false,
    /*uvCoordFromMissingMatrix=*/false,
};

// QuadColorFillShader: the quad-edge-AA fill with no color processors; a coverage draw is either a
// direct analytic clip (runtime uniforms) or a device-space mask, which raises HAS_MASK_TEXTURE.
constexpr AOTExecutorRecord kQuadColorRecord = {
    QuadColorFillShader::Name,
    GeometryBit(AOTGeometryKind::QuadPerEdgeAA),
    /*servesAlphaOnly=*/false,
    AOTRecordColorShape::None,
    static_cast<uint8_t>(CoverageBit(CoverageKind::AARect) | CoverageBit(CoverageKind::RRect) |
                         CoverageBit(CoverageKind::DeviceMask)),
    /*coverageRequiresColorFromPaint=*/false,
    /*localMaskRaisesAxis=*/false,
    /*deviceMaskRaisesAxis=*/true,
    /*rejectsPerspectiveSubset=*/false,
    /*uvCoordFromMissingMatrix=*/false,
};

// MeshFillShader: the triangle mesh with no fragment processors; the tex-coord axis mirrors the
// vertex attributes, and the per-vertex color is an unconditional attribute.
constexpr AOTExecutorRecord kMeshRecord = {
    MeshFillShader::Name,
    GeometryBit(AOTGeometryKind::Mesh),
    /*servesAlphaOnly=*/false,
    AOTRecordColorShape::None,
    /*coverageKindMask=*/0,
    /*coverageRequiresColorFromPaint=*/false,
    /*localMaskRaisesAxis=*/false,
    /*deviceMaskRaisesAxis=*/false,
    /*rejectsPerspectiveSubset=*/false,
    /*uvCoordFromMissingMatrix=*/false,
    /*texCoordsFromGeometry=*/true,
};

const AOTExecutorRecord* const kRecords[] = {&kQuadTextureRecord, &kSolidColorRecord,
                                             &kQuadColorRecord, &kMeshRecord};

}  // namespace

std::optional<AOTExecutorInputs> MatchExecutorRecord(const AOTExecutorRecord& record,
                                                     const AOTDrawFacts& facts) {
  if (!record.servesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  const auto geometryBit = static_cast<uint32_t>(facts.geometry.kind);
  if (geometryBit >= 32 || (record.geometryKindMask & (1u << geometryBit)) == 0) {
    return std::nullopt;
  }
  if (record.colorShape == AOTRecordColorShape::None && facts.colorFPCount != 0) {
    return std::nullopt;
  }
  if (record.colorShape == AOTRecordColorShape::BareTexture && facts.colorFPCount != 1) {
    return std::nullopt;
  }
  AOTExecutorInputs inputs;
  // The coverage stage: absent, or exactly one classification the record admits. Anything else -
  // a second coverage processor, a composed clip, an unclassifiable term - is not this shape.
  if (facts.fragmentCount != facts.colorFPCount) {
    if (facts.fragmentCount != facts.colorFPCount + 1 || !facts.coverage) {
      return std::nullopt;
    }
    const auto coverageBit = static_cast<uint8_t>(1u << static_cast<uint32_t>(*facts.coverage));
    if ((record.coverageKindMask & coverageBit) == 0) {
      return std::nullopt;
    }
    if (record.coverageRequiresColorFromPaint && !facts.colorFromPaint) {
      return std::nullopt;
    }
    inputs.hasLocalMask = record.localMaskRaisesAxis && *facts.coverage == CoverageKind::LocalMask;
    inputs.hasMaskTexture =
        record.deviceMaskRaisesAxis && *facts.coverage == CoverageKind::DeviceMask;
  }
  // The color shape: one bare texture leaf (no operators above it). Without an IR the live sole
  // color processor answers the same question; its sampler count guards a failed instantiation,
  // which the IR's leaf existence already implies.
  bool leafHasPerspective = false;
  if (record.colorShape == AOTRecordColorShape::BareTexture) {
    if (facts.ir != nullptr) {
      AOTChainBuilder::AOTTextureTailShape tail = {};
      if (!AOTChainBuilder::CollectTextureTail(facts.ir->color, &tail) || !tail.ops.empty() ||
          tail.leaf->isYUV || tail.leaf->samplingKind != AOTTextureSamplingKind::Plain) {
        return std::nullopt;
      }
      leafHasPerspective = tail.leaf->hasPerspective;
    } else {
      if (facts.info == nullptr) {
        return std::nullopt;
      }
      auto* fp = facts.info->getFragmentProcessor(0);
      if (IsProcessor<TextureEffect>(fp)) {
        auto* texture = static_cast<const TextureEffect*>(fp);
        if (texture->numTextureSamplers() == 0 || texture->isYUV()) {
          return std::nullopt;
        }
        leafHasPerspective = texture->hasPerspective();
      } else {
        return std::nullopt;
      }
    }
  }
  const int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  inputs.hasUVCoord = record.uvCoordFromMissingMatrix ? !facts.geometry.hasUVMatrix
                                                      : facts.geometry.hasUVMatrix;
  inputs.hasSubset = facts.geometry.hasSubset;
  inputs.hasTexCoords = record.texCoordsFromGeometry && facts.geometry.hasTexCoords;
  if (record.rejectsPerspectiveSubset && inputs.hasSubset &&
      (facts.geometry.hasUVPerspective || leafHasPerspective)) {
    return std::nullopt;
  }
  inputs.xpType = xpType;
  return inputs;
}

const AOTExecutorRecord* FindExecutorRecord(const char* family) {
  if (family == nullptr) {
    return nullptr;
  }
  for (auto* record : kRecords) {
    if (std::string_view(record->family) == family) {
      return record;
    }
  }
  return nullptr;
}

}  // namespace tgfx
