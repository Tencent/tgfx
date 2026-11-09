// Gradient fragment hook: the border clamp, the premultiply and the modulating input color alpha,
// verbatim from the former kernels/shape_instanced_texture_coverage.frag's GRADIENT branch. The
// layout maths live in the shared source/gradient_layout.inc.
#include "source/gradient_layout.inc"

void Gradient_fragment(inout Fragment f) {
  highp float t =
      gradientLayoutT(Gradient_LayoutType, Gradient_Bias, Gradient_Scale, Gradient_vCoord);
  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = Gradient_LeftBorderColor;
  } else if (t >= 1.0) {
    gradColor = Gradient_RightBorderColor;
  } else {
    gradColor = mix(Gradient_Start, Gradient_End, t);
  }
  gradColor.rgb *= gradColor.a;
  gradColor *= f.color.a;
  f.color = gradColor;
}
