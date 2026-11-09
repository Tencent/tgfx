// MaskTexture fragment hook: the coverage mask's red channel multiplies the running coverage. The
// expression is verbatim from the former fill pipeline's FILL_COVERAGE_VERTEX_MASK.
void MaskTexture_fragment(inout Fragment f) {
  f.coverage *= vec4(texture(MaskTexture_Sampler, MaskTexture_vCoord).r);
}
