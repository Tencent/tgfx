// The glass shape's refraction direction for the rounded-rect form: the SDF, the edge band
// weight, the Figma-profile offset distance, and the corner-compensated blend of the gradient
// and center directions. The body is the text route's emission, translated line for line; the
// compile-time shape choice picks this function (the ellipse form has its own).
//@function tgxxGlassSDFRounded
//@op GlassSDFRounded
//@frag
vec4 tgxxGlassSDFRounded(vec4 inputColor /*@in*/, vec4 GlassShapeP0, vec4 GlassShapeP1) {
  vec2 glassUV = inputColor.xy;
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassShapeP0.x;
  float halfH = GlassShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  float cornerRadius = GlassShapeP0.z;
  float dx = abs(px) - halfW + cornerRadius;
  float dy = abs(py) - halfH + cornerRadius;
  float outside = length(max(vec2(dx, dy), vec2(0.0)));
  float outerSDF = outside + min(max(dx, dy), 0.0) - cornerRadius;
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
  vec2 absP = abs(vec2(px, py));
  float refractionDistance = GlassShapeP0.w;
  float r_effective = min(min(halfW, halfH), max(cornerRadius, refractionDistance));
  vec2 cornerCenter = vec2(halfW - r_effective, halfH - r_effective);
  vec2 relToCorner = absP - cornerCenter;
  float compensation = max(-min(relToCorner.x, relToCorner.y), 0.0);
  vec2 grad = relToCorner + vec2(compensation);
  float qx = absP.x - cornerCenter.x;
  float qy = absP.y - cornerCenter.y;
  float straightHalfW = max(cornerCenter.x, 0.0001);
  float straightHalfH = max(cornerCenter.y, 0.0001);
  float cornerWeightX = smoothstep(0.0, straightHalfW, absP.x);
  float cornerWeightY = smoothstep(0.0, straightHalfH, absP.y);
  float cornerWeight = (qx > 0.0 && qy > 0.0) ? 1.0 : ((qx > qy) ? cornerWeightY : cornerWeightX);
  effectiveSplay = min(cornerWeight + GlassShapeP1.y, 1.0);
  float gradientLength = length(grad);
  vec2 gradientDir = vec2(0.0);
  if (gradientLength > 0.000001) {
    gradientDir = -vec2(sign(px) * grad.x, sign(py) * grad.y) / gradientLength;
  }
  float centerDistance = length(vec2(px, py));
  vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
  vec2 refractDir = mix(gradientDir, centerDir, effectiveSplay);
  float refractLength = length(refractDir);
  refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
  return vec4(refractDir, offsetDist, edgeWeight);
}
