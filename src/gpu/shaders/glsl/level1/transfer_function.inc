// Shared ColorSpaceXform transfer function. Single
// source for the pointwise operator kernels (per-slot src_tf/dst_tf wrappers) and the chain
// kernel's OP_COLOR_SPACE_XFORM slot. Parameters are passed explicitly so each caller can feed
// its own uniform names: tf0 packs (G, A, B, C), tf1 packs (D, E, F), tfType selects the curve
// family.
//
// The curve is applied to r, g and b in one call. The runtime (ShaderBuilder::
// appendColorGamutXform) calls a scalar function once per channel; a per-channel call here would
// put six inlined copies of the four-way curve selection into every kernel. Each lane performs the
// runtime's scalar operations in the same order, and mix() with a boolean vector is a true
// component-wise select (SPIR-V OpSelect): a lane keeps exactly the arm the scalar ternary would
// have taken, and whatever the other arm computed on that lane (NaN included) is discarded.
#ifndef TGFX_TRANSFER_FUNCTION_INC
#define TGFX_TRANSFER_FUNCTION_INC

vec3 transferTF(vec3 x, vec4 tf0, vec4 tf1, int tfType) {
  vec3 s = sign(x);
  x = abs(x);
  float A = tf0[1], B = tf0[2], C = tf0[3];
  float D = tf1[0], E = tf1[1], F = tf1[2];
  if (tfType == 0) {
    float G = tf0[0];
    x = mix(pow(A * x + B, vec3(G)) + E, C * x + F, lessThan(x, vec3(D)));
  } else if (tfType == 1) {
    vec3 xC = pow(x, vec3(C));
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), vec3(F));
  } else if (tfType == 2) {
    x = mix(exp((x - E) * C) + D, pow(x * A, vec3(B)), lessThanEqual(x * A, vec3(1.0)));
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = mix(C * log(x - D) + E, A * pow(x, vec3(B)), lessThanEqual(x, vec3(1.0)));
  }
  return s * x;
}

#endif
