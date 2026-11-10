// GlassLocalMask fragment hook: the SrcIn mask's alpha multiplies the running coverage, gated by
// the runtime HasMask flag (the former HasLocalMask branch — a draw without a mask binds the
// shared dummy texture on the never-sampled slot). The mask only scales the coverage after the
// refraction sample, so the branch cannot move a texture coordinate.
void GlassLocalMask_fragment(inout Fragment f) {
  if (GlassLocalMask_HasMask != 0) {
    f.coverage *= vec4(texture(GlassLocalMask_Sampler, GlassLocalMask_vCoord).a);
  }
}
