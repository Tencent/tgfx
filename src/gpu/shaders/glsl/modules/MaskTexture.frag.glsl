// MaskTexture fragment hook: the coverage mask's red channel multiplies the running coverage. The
// expression is bit-exact by contract.
void MaskTexture_fragment(inout Fragment f) {
  f.coverage *= vec4(texture(MaskTexture_Sampler, MaskTexture_vCoord).r);
}
