//@function xpBlend_HardLight
//@op XpBlendHardLight
//@frag
vec4 xpBlend_HardLight(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  // Overlay and HardLight share one branch: Overlay == HardLight with operands swapped.
  // Two structurally-related branches in the if-chain miscompile under Chrome WebGPU (dawn
  // swaps their bodies in large shaders), so they are fused with an operand pick instead.
  // A is the branch-control operand (the backdrop for Overlay, the source for HardLight) and
  // B the other side, making the math a literal translation of the runtime HardLight emission
  // (fast multiply path when 2 * control < control.a); the mix keeps the selection branchless
  // and equally dawn-safe.
  result.a = S.a + (1.0 - S.a) * D.a;
  vec3 pick = step(vec3(1.5), vec3(float(20 - 14)));
  vec4 A = mix(D, S, pick.x);  // 20 15: A = D; 20 20: A = S
  vec4 B = mix(S, D, pick.x);  // 20 15: B = S; 20 20: B = D
  vec3 fast = 2.0 * A.rgb * B.rgb;
  vec3 slow = vec3(A.a * B.a) - 2.0 * (vec3(A.a) - A.rgb) * (vec3(B.a) - B.rgb);
  result.rgb = mix(slow, fast, lessThan(2.0 * A.rgb, vec3(A.a)));
  result.rgb += A.rgb * (1.0 - B.a) + B.rgb * (1.0 - A.a);

  return result;
}
