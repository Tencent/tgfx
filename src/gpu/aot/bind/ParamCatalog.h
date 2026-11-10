// The declared-name catalog for uniform fields whose writers still live in the runtime
// GLSLProcessor files (the geometry processors' shared fields, the Glass processors, and the
// gradient colorizers' JIT-declared forms): declaration only, no writer moves onto it yet — the
// runtime-retirement step folds those writers into modules and reuses these entries. The build
// time reconciliation checks every reflected uniform name against this catalog (plus the module
// Fields lists), so a GLSL field nobody declared here fails the bundle build.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct ParamCatalog {
  // The geometry processors' shared fields.
  static constexpr Param<ParamType::Float4> Color{"Color"};
  static constexpr Param<ParamType::Int> HasCommonColor{"HasCommonColor"};
  static constexpr Param<ParamType::Float3x3> Matrix{"Matrix"};
  static constexpr Param<ParamType::Float3x3> TexSubsetMatrix{"texSubsetMatrix"};
  static constexpr Param<ParamType::Float> Coverage{"Coverage"};
  static constexpr Param<ParamType::Int> AAEnabled{"AAEnabled"};
  static constexpr Param<ParamType::Int> StrokeEnabled{"StrokeEnabled"};

  // Fields the runtime GLSLProcessor writers produce for the precompiled kernels: the GP coord
  // transform matrices (numbered by slot), the chain's per-slot const-color records, the fused
  // blend's operand order, and the atlas text size. Their writers stay on the runtime side.
  static constexpr Param<ParamType::Float3x3> CoordTransformMatrix{"CoordTransformMatrix"};

  // The chain's prefixed color-space fields (ColorSpaceXformHelper's prefix form).
  static constexpr Param<ParamType::Int> ChainCSFlags{"ChainCSFlags"};
  static constexpr Param<ParamType::Float4> ChainSrcTF0{"ChainSrcTF0"};
  static constexpr Param<ParamType::Float4> ChainSrcTF1{"ChainSrcTF1"};
  static constexpr Param<ParamType::Int> ChainSrcTFType{"ChainSrcTFType"};
  static constexpr Param<ParamType::Float4> ChainSrcOOTF{"ChainSrcOOTF"};
  static constexpr Param<ParamType::Float3x3> ChainColorXform{"ChainColorXform"};
  static constexpr Param<ParamType::Float4> ChainDstOOTF{"ChainDstOOTF"};
  static constexpr Param<ParamType::Float4> ChainDstTF0{"ChainDstTF0"};
  static constexpr Param<ParamType::Float4> ChainDstTF1{"ChainDstTF1"};
  static constexpr Param<ParamType::Int> ChainDstTFType{"ChainDstTFType"};

  static constexpr ParamInfo Fields[] = {
      Color,        HasCommonColor,  Matrix,        TexSubsetMatrix,
      Coverage,     AAEnabled,       StrokeEnabled, CoordTransformMatrix,
      ChainCSFlags, ChainSrcTF0,     ChainSrcTF1,   ChainSrcTFType,
      ChainSrcOOTF, ChainColorXform, ChainDstOOTF,  ChainDstTF0,
      ChainDstTF1,  ChainDstTFType};
};

}  // namespace tgfx
