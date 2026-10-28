// Shader-side leaf tiling with three array recipes, used by the gradient resource kind
// (RESOURCE_KIND 1) of PointwiseChainShader. Pairs with pointwise_tiled3_uniforms.inc.

// The number of recipe sets, and the leaf that reads recipe set `recipe` (-1 = unused). Pure
// selector reads: main() uses them to find the tiled leaves without touching the chainTiled* state.
#define CHAIN_TILED_RECIPES 3
int chainTiledRecipeLeaf(int recipe) {
  return TiledRecipeModes[recipe].x;
}

// Loads the recipe of leafIndex into the chainTiled* state (pointwise_tiled_sampling.inc) and
// returns true when the leaf is tiled.
bool chainLoadTiledRecipe(int leafIndex, out bool strict) {
  strict = false;
  int recipeIndex = -1;
  for (int index = 0; index < 3; ++index) {
    if (TiledRecipeModes[index].x == leafIndex) {
      recipeIndex = index;
    }
  }
  if (recipeIndex >= 0) {
    ivec4 recipe = TiledRecipeModes[recipeIndex];
    chainTiledModeX = recipe.y;
    chainTiledModeY = recipe.z;
    chainTiledSubset = TiledRecipeSubsets[recipeIndex];
    chainTiledClamp = TiledRecipeClamps[recipeIndex];
    chainTiledDimension = TiledRecipeDimensions[recipeIndex];
    strict = recipe.w != 0;
    return true;
  }
  return false;
}
