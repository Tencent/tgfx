// LocalMask fragment hook: the mask's alpha multiplies the running coverage (the former
// FILL_LOCAL_MASK). The expression is verbatim from the former fill pipeline's fillMaskStage.
void LocalMask_fragment(inout Fragment f) {
  highp vec2 localMaskCoord = LocalMask_vCoord.xy / LocalMask_vCoord.z;
  f.coverage *= vec4(texture(LocalMask_Sampler, localMaskCoord).a);
}
