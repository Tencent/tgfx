// Fields of the three-recipe tiling module (pointwise_tiled3.inc), included inside the
// FragmentUniformBlock by the kernel entry. Recipe k serves the leaf TiledRecipeModes[k].x
// (-1 = unused) with modes .y/.z and strictness .w.
  ivec4 TiledRecipeModes[3];
  vec4 TiledRecipeSubsets[3];
  vec4 TiledRecipeClamps[3];
  vec2 TiledRecipeDimensions[3];
