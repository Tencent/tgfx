// Unified analytic clip coverage, selected by the HasClip runtime uniform:
//   0 = no clip; 1 = device-space AA rect (Rect, uploaded half a pixel outward);
//   2 = rounded rect (LocalRect/RadiiX/RadiiY/AntiAlias/DeviceToLocal);
//   3 = local-space AA rect (LocalRect/DeviceToLocal).
// The rrect and local-rect math is a line-by-line translation of GLSLRRectEffect /
// GLSLRectEffect emitCode so the precompiled kernels stay pixel-identical to the JIT route.
float rrectClipCoverage() {
  highp vec3 hl = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
  highp vec2 local = hl.xy / hl.z;
  highp vec4 cornersX = vec4(LocalRect.x, LocalRect.z, LocalRect.z, LocalRect.x);
  highp vec4 cornersY = vec4(LocalRect.y, LocalRect.y, LocalRect.w, LocalRect.w);
  vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);
  vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);
  highp vec4 dx = (vec4(local.x) - cornersX) * signsX;
  highp vec4 dy = (vec4(local.y) - cornersY) * signsY;
  vec4 inBox = step(dx, RadiiX) * step(dy, RadiiY);
  vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
  inBox *= step(prefixSum, vec4(0.5));
  float inAnyCorner = dot(inBox, vec4(1.0));
  highp vec4 arcCentersX = cornersX + signsX * RadiiX;
  highp vec4 arcCentersY = cornersY + signsY * RadiiY;
  highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
  highp vec2 r = vec2(dot(inBox, RadiiX), dot(inBox, RadiiY));
  highp vec2 safeRadii = max(r, vec2(0.5));
  highp vec2 offset = (local - arcCenter) / safeRadii;
  highp vec2 safeOffset = max(abs(offset), vec2(1.0 / 4096.0));
  highp float test = dot(safeOffset, safeOffset) - 1.0;
  highp vec2 grad = 2.0 * safeOffset / safeRadii;
  highp float gradDot = max(dot(grad, grad), 1.1755e-38);
  highp float cornerDist = test * inversesqrt(gradDot);
  highp vec2 d0 = local - LocalRect.xy;
  highp vec2 d1 = LocalRect.zw - local;
  highp float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));
  highp float dist = mix(edgeDist, cornerDist, inAnyCorner);
  highp float aaCov = clamp(0.5 - dist, 0.0, 1.0);
  highp float nonAACov = step(dist, 0.0);
  return mix(nonAACov, aaCov, AntiAlias);
}

float clipCoverage() {
  if (HasClip == 2) {
    return rrectClipCoverage();
  }
  if (HasClip == 3) {
    highp vec3 hl = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
    highp vec2 local = hl.xy / hl.z;
    highp vec4 dAA = clamp(vec4(local - LocalRect.xy, LocalRect.zw - local), -0.5, 0.5);
    highp vec2 aaCovXY = dAA.xy + dAA.zw;
    return aaCovXY.x * aaCovXY.y;
  }
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                                 vec4(gl_FragCoord.xyxy - Rect),
                             0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return HasClip != 0 ? dists2.x * dists2.y : 1.0;
}
