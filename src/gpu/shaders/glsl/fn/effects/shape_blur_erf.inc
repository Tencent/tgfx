// Approximates erf with the rational form from Hastings (1955), published as Abramowitz & Stegun
// formula 7.1.27 (p. 299).
//   x       the argument
//   return  erf(x), in (-1, 1)
//
// Valid for 0 <= x < inf with a stated error bound of 5e-4. The sign is applied afterwards since
// erf is odd.
//@function shapeBlurErf
highp float shapeBlurErf(highp float x) {
  highp float ax = abs(x);
  highp float d = 1.0 + ax * (0.278393 + ax * (0.230389 + ax * (0.000972 + ax * 0.078108)));
  highp float d2 = d * d;
  highp float positiveBranch = 1.0 - 1.0 / (d2 * d2);
  return x >= 0.0 ? positiveBranch : -positiveBranch;
}
