// LocalMask fragment hook: the mask's alpha multiplies the running coverage. The expression is
// bit-exact by contract.
void LocalMask_fragment(inout Fragment f) {
  highp vec2 localMaskCoord = LocalMask_vCoord.xy / LocalMask_vCoord.z;
  f.coverage *= vec4(texture(LocalMask_Sampler, localMaskCoord).a);
}
