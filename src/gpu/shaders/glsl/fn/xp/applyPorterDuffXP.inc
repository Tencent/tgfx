// The one signature the text route, the GL assembly and the Vulkan module all spell: every input
// — the destination sampler included — arrives as an argument. The zero-coverage discard is not
// optional: the dst texture is a pre-draw copy (or the barrier-less target itself), so a
// zero-coverage fragment writing the dst back would erase what an earlier primitive of the same
// draw already blended there.
//@function applyPorterDuffXP
vec4 applyPorterDuffXP(vec4 srcColor, vec4 coverage, vec2 fragCoord, vec2 dstUpperLeft,
                       vec2 dstCoordScale, int blendMode, sampler2D dstTex) {
  if (coverage.r <= 0.0 && coverage.g <= 0.0 && coverage.b <= 0.0) {
    discard;
  }
  highp vec2 dstCoord = (fragCoord - dstUpperLeft) * dstCoordScale;
  vec4 dstColor = texture(dstTex, dstCoord);
  return xpBlendWithCoverage(srcColor, dstColor, coverage, blendMode);
}
