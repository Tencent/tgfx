// Shader-side leaf tiling with two scalar recipes (Tiled* and Tiled*2), used by
// PointwiseChainShader and the NoiseGraph profile of PointwiseGraphShader. Pairs with
// pointwise_tiled2_uniforms.inc.

// Loads the recipe of leafIndex into the chainTiled* state (pointwise_tiled_sampling.inc) and
// returns true when the leaf is tiled.
bool chainLoadTiledRecipe(int leafIndex, out bool strict) {
  strict = false;
  if (leafIndex == TiledLeafIndex || leafIndex == TiledLeafIndex2) {
    bool secondRecipe = leafIndex == TiledLeafIndex2;
    chainTiledModeX = secondRecipe ? TiledModeX2 : TiledModeX;
    chainTiledModeY = secondRecipe ? TiledModeY2 : TiledModeY;
    chainTiledSubset = secondRecipe ? TiledSubset2 : TiledSubset;
    chainTiledClamp = secondRecipe ? TiledClamp2 : TiledClamp;
    chainTiledDimension = secondRecipe ? TiledDimension2 : TiledDimension;
    strict = (secondRecipe ? TiledStrict2 : TiledStrict) != 0;
    return true;
  }
  return false;
}
