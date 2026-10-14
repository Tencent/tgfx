// Shader-side leaf tiling with three array recipes, used by the TiledGraph profile of
// PointwiseGraphShader. Pairs with pointwise_tiled3_uniforms.inc.

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
