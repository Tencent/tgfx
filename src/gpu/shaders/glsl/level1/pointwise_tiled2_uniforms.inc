// Fields of the two-recipe tiling module (pointwise_tiled2.inc), included inside the
// FragmentUniformBlock by the kernel entry.
  // Tiled leaf support: up to two leaves go through the shader-side tiling (wrap or border
  // emulation), each with its own recipe. TiledLeafIndex selects the leaf that reads the first
  // recipe (Tiled*), TiledLeafIndex2 the leaf that reads the second (Tiled*2); -1 = none. The
  // recipes are independent: a drop shadow's source and offset shadow children resolve to
  // different clamp rects, and sharing one recipe would clamp the second leaf to the first's
  // domain.
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
