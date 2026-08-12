// Shared ColorSpaceXform transfer function (mirrors textured_color_space_xform.frag). Single
// source for the pointwise operator kernels (per-slot src_tf/dst_tf wrappers) and the chain
// kernel's OP_COLOR_SPACE_XFORM slot. Parameters are passed explicitly so each caller can feed
// its own uniform names: tf0 packs (G, A, B, C), tf1 packs (D, E, F), tfType selects the curve
// family.
#ifndef TGFX_TRANSFER_FUNCTION_INC
#define TGFX_TRANSFER_FUNCTION_INC

float transferTF(float x, vec4 tf0, vec4 tf1, int tfType) {
  float s = sign(x);
  x = abs(x);
  float A = tf0[1], B = tf0[2], C = tf0[3];
  float D = tf1[0], E = tf1[1], F = tf1[2];
  if (tfType == 0) {
    float G = tf0[0];
    x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
  } else if (tfType == 1) {
    float xC = pow(x, C);
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
  } else if (tfType == 2) {
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
  }
  return s * x;
}

#endif
