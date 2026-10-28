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
  static constexpr Param<ParamType::Float3x3> ViewMatrix{"ViewMatrix"};
  static constexpr Param<ParamType::Float3x3> UVMatrix{"UVMatrix"};
  static constexpr Param<ParamType::Float3x3> TexSubsetMatrix{"texSubsetMatrix"};
  static constexpr Param<ParamType::Float> Coverage{"Coverage"};
  static constexpr Param<ParamType::Int> AAEnabled{"AAEnabled"};
  static constexpr Param<ParamType::Int> StrokeEnabled{"StrokeEnabled"};

  // Fields the runtime GLSLProcessor writers produce for the precompiled kernels: the GP coord
  // transform matrices (numbered by slot), the chain's per-slot const-color records, the fused
  // blend's operand order, and the atlas text size. Their writers stay on the runtime side.
  static constexpr Param<ParamType::Float3x3> CoordTransformMatrix{"CoordTransformMatrix"};
  static constexpr Param<ParamType::Float4> ConstColorValue{"ConstColorValue"};
  static constexpr Param<ParamType::Int> ConstInputMode{"ConstInputMode"};
  static constexpr Param<ParamType::Int> BlendConstFirst{"BlendConstFirst"};
  static constexpr Param<ParamType::Float2> AtlasSizeInv{"atlasSizeInv"};

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

  // The Glass processors' fields.
  static constexpr Param<ParamType::Float4> GlassOpticsP0{"GlassOpticsP0"};
  static constexpr Param<ParamType::Float4> GlassOpticsP1{"GlassOpticsP1"};
  static constexpr Param<ParamType::Float4> GlassOpticsP2{"GlassOpticsP2"};
  static constexpr Param<ParamType::Float4> GlassOpticsP3{"GlassOpticsP3"};
  static constexpr Param<ParamType::Int> LightingOn{"LightingOn"};
  static constexpr Param<ParamType::Float4> GlassShapeP0{"GlassShapeP0"};
  static constexpr Param<ParamType::Float4> GlassShapeP1{"GlassShapeP1"};
  static constexpr Param<ParamType::Float2> GlassEdgeSpan{"GlassEdgeSpan"};
  static constexpr Param<ParamType::Float4> GlassFineMaskUV{"GlassFineMaskUV"};
  static constexpr Param<ParamType::Float4> GlassEdgeMaskUV{"GlassEdgeMaskUV"};
  static constexpr Param<ParamType::Int> EdgeMaskOn{"EdgeMaskOn"};
  static constexpr Param<ParamType::Float2> GlassUDFRadius{"GlassUDFRadius"};
  static constexpr Param<ParamType::Float2> GlassUDFStep{"GlassUDFStep"};
  static constexpr Param<ParamType::Int> Field{"Field"};
  static constexpr Param<ParamType::Int> InputIsPacked{"InputIsPacked"};

  static constexpr ParamInfo Fields[] = {
      Color,           HasCommonColor, Matrix,          ViewMatrix,      UVMatrix,
      TexSubsetMatrix, Coverage,       AAEnabled,       StrokeEnabled,   CoordTransformMatrix,
      ConstColorValue, ConstInputMode, BlendConstFirst, AtlasSizeInv,    ChainCSFlags,
      ChainSrcTF0,     ChainSrcTF1,    ChainSrcTFType,  ChainSrcOOTF,    ChainColorXform,
      ChainDstOOTF,    ChainDstTF0,    ChainDstTF1,     ChainDstTFType,  GlassOpticsP0,
      GlassOpticsP1,   GlassOpticsP2,  GlassOpticsP3,   LightingOn,      GlassShapeP0,
      GlassShapeP1,    GlassEdgeSpan,  GlassFineMaskUV, GlassEdgeMaskUV, EdgeMaskOn,
      GlassUDFRadius,  GlassUDFStep,   Field,           InputIsPacked};
};

}  // namespace tgfx
