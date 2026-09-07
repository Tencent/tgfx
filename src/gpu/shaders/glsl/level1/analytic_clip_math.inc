// Parameterized analytic clip coverage math, shared by the level1 runtime clip contract
// (clip_coverage.inc) and the pointwise chain kernel (OP_LOCAL_RECT_COVERAGE / OP_RRECT_COVERAGE).
// The bodies are line-by-line translations of GLSLRectEffect / GLSLRRectEffect emitCode so every
// consumer stays pixel-identical to the JIT route.
// analyticLocalRectCoverage: rect is exact (no outset); the AA ramp clamps each edge distance to
// half a pixel. analyticRRectCoverage: arc-box dispatch with Sampson-distance corner SDF and the
// AA / nonAA mix driven by the antiAlias uniform value.

float analyticLocalRectCoverage(vec4 rect, mat3 deviceToLocal) {
  highp vec3 hl = deviceToLocal * vec3(gl_FragCoord.xy, 1.0);
  highp vec2 local = hl.xy / hl.z;
  highp vec4 dAA = clamp(vec4(local - rect.xy, rect.zw - local), -0.5, 0.5);
  highp vec2 aaCovXY = dAA.xy + dAA.zw;
  return aaCovXY.x * aaCovXY.y;
}

float analyticRRectCoverage(vec4 rect, vec4 radiiX, vec4 radiiY, float antiAlias,
                            mat3 deviceToLocal) {
  highp vec3 hl = deviceToLocal * vec3(gl_FragCoord.xy, 1.0);
  highp vec2 local = hl.xy / hl.z;
  highp vec4 cornersX = vec4(rect.x, rect.z, rect.z, rect.x);
  highp vec4 cornersY = vec4(rect.y, rect.y, rect.w, rect.w);
  vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);
  vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);
  highp vec4 dx = (vec4(local.x) - cornersX) * signsX;
  highp vec4 dy = (vec4(local.y) - cornersY) * signsY;
  vec4 inBox = step(dx, radiiX) * step(dy, radiiY);
  vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
  inBox *= step(prefixSum, vec4(0.5));
  float inAnyCorner = dot(inBox, vec4(1.0));
  highp vec4 arcCentersX = cornersX + signsX * radiiX;
  highp vec4 arcCentersY = cornersY + signsY * radiiY;
  highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
  highp vec2 r = vec2(dot(inBox, radiiX), dot(inBox, radiiY));
  highp vec2 safeRadii = max(r, vec2(0.5));
  highp vec2 offset = (local - arcCenter) / safeRadii;
  highp vec2 safeOffset = max(abs(offset), vec2(1.0 / 4096.0));
  highp float test = dot(safeOffset, safeOffset) - 1.0;
  highp vec2 grad = 2.0 * safeOffset / safeRadii;
  highp float gradDot = max(dot(grad, grad), 1.1755e-38);
  highp float cornerDist = test * inversesqrt(gradDot);
  highp vec2 d0 = local - rect.xy;
  highp vec2 d1 = rect.zw - local;
  highp float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));
  highp float dist = mix(edgeDist, cornerDist, inAnyCorner);
  highp float aaCov = clamp(0.5 - dist, 0.0, 1.0);
  highp float nonAACov = step(dist, 0.0);
  return mix(nonAACov, aaCov, antiAlias);
}
