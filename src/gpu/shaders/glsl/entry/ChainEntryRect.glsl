// The rect-geometry, leaf-free, no-xp interface chunk, extracted verbatim from the assembled
// variant text (byte-for-byte writer compatibility: the uniform block is the declaration the
// ProgramWriter uploads through), plus the fixed designator sources this geometry spells. The
// assembled text carries no spec constants and no gates: the structure is in the code.
layout(std140) uniform FragmentUniformBlock {
  vec4 Color;
  int HasCommonColor;
  int SlotCount;
  ivec4 SlotPacked[16];
  int CoverageRootIndex;
  int RootIndex;
  int ClipCoverageRegister;
  int UsedOps;
  int BlendModes;
  int TextureFlags;
  int GradientKinds;
  int NeedsBlendMath;
  vec4 Subset;
  int SubsetClampOn;
  int ChainCSFlags;
  vec4 ChainSrcTF0;
  vec4 ChainSrcTF1;
  int ChainSrcTFType;
  vec4 ChainSrcOOTF;
  mat3 ChainColorXform;
  vec4 ChainDstOOTF;
  vec4 ChainDstTF0;
  vec4 ChainDstTF1;
  int ChainDstTFType;
  vec4 ChainData[144];
  mat3 DeviceCoordMatrix;
  vec4 DeviceMaskSubset;
  int TiledLeafIndex;
  int TiledLeafIndex2;
  int TiledModeX;
  int TiledModeY;
  vec4 TiledSubset;
  vec4 TiledClamp;
  vec2 TiledDimension;
  int TiledStrict;
  int TiledModeX2;
  int TiledModeY2;
  vec4 TiledSubset2;
  vec4 TiledClamp2;
  vec2 TiledDimension2;
  int TiledStrict2;
  vec4 ChainColorParams_Color;
  int ChainColorParams_HasCommonColor;
  int ChainColorParams_StrokeEnabled;
  int ChainColorParams_AAEnabled;
  float ChainColorParams_Coverage;
  int OutputAlphaSwizzle;
};
 uniform sampler2D MaskTextureSampler;
layout(location = 0) out vec4 fragColor;
 in float vCoverage;
 in vec4 vColor;

// The fixed designator sources for this geometry: the common-color-or-vertex-color selection and
// the coverage varying the -1/-3 designators read.
vec4 chainGeomColorSource() {
  return (ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor;
}

vec4 chainUnitCoverageSource() {
  return vec4(vCoverage);
}

vec4 chainOpaqueGeomColorSource() {
  vec4 color = (ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor;
  return vec4(color.rgb, 1.0);
}
