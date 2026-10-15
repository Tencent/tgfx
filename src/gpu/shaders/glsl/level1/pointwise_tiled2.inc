// Shader-side leaf tiling with two scalar recipes (Tiled* and Tiled*2), used by
// PointwiseChainShader and the NoiseGraph profile of PointwiseGraphShader. Pairs with
// pointwise_tiled2_uniforms.inc.

// The number of recipe sets, and the leaf that reads recipe set `recipe` (-1 = unused). Pure
// selector reads: main() uses them to find the tiled leaves without touching the chainTiled* state.
#define CHAIN_TILED_RECIPES 2
int chainTiledRecipeLeaf(int recipe) {
  return recipe == 0 ? TiledLeafIndex : TiledLeafIndex2;
}

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
