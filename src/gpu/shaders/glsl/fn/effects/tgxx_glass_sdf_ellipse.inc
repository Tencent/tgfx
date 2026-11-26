// The glass shape's refraction direction for the ellipse form: the normalized-distance SDF, the
// edge band weight, the Figma-profile offset distance, and the ellipse gradient blended with the
// center direction. The body is the text route's emission, translated line for line; the
// compile-time shape choice picks this function (the rounded-rect form has its own).
//@function tgxxGlassSFDEllipse
//@op GlassSFDEllipse
//@frag
vec4 tgxxGlassSFDEllipse(vec4 inputColor /*@in*/, vec4 GlassShapeP0, vec4 GlassShapeP1) {
  vec2 glassUV = inputColor.xy;
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassShapeP0.x;
  float halfH = GlassShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  float normalizedDist = length(vec2(px / halfW, py / halfH));
  float gradientLength0 = length(vec2(px / (halfW * halfW), py / (halfH * halfH)));
  float outerSDF = normalizedDist * (normalizedDist - 1.0) / max(gradientLength0, 0.000001);
  if (gradientLength0 < 0.000001) {
    outerSDF = -min(halfW, halfH);
  }
  if (outerSDF >= 0.0) {
    return vec4(0.0);
  }
  float edgeDist = -outerSDF;
  float edgeBand = max(1.0, GlassShapeP1.w);
  float edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDist);
  float rd = max(GlassShapeP0.w, 0.0001);
  float heightT = clamp((rd - edgeDist) / rd, 0.0, 1.0);
  float offsetDist = min(GlassShapeP1.x * rd * pow(heightT, 3.5), 0.999 * rd);
  float effectiveSplay = GlassShapeP1.y;
  vec2 sdfGradient = vec2(px / (halfW * halfW), py / (halfH * halfH));
  float gradientLength = length(sdfGradient);
  vec2 gradientDir = vec2(0.0);
  if (gradientLength > 0.000001) {
    gradientDir = -sdfGradient / gradientLength;
  }
  float centerDistance = length(vec2(px, py));
  vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
  vec2 refractDir = mix(gradientDir, centerDir, effectiveSplay);
  float refractLength = length(refractDir);
  refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
  return vec4(refractDir, offsetDist, edgeWeight);
}
