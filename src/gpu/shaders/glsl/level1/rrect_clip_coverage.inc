// 来源: WP3 新文件（2026-09-07 19:41 写入 tgfx/src/gpu/shaders/glsl/level1/rrect_clip_coverage.inc，
//       2988 字节；19:52 时 tgfx 内该文件已不可读，此为上下文重建版）
// 这是 WP3 的核心产出：从 GLSLRRectEffect::emitCode 逐行移植的 Sampson 椭圆 SDF。

// Analytic AA rounded-rect clip coverage. Ported line-for-line from
// GLSLRRectEffect::emitCode so the precompiled kernel and the runtime-generated shader evaluate
// the identical Sampson ellipse SDF. Requires the contract fields LocalRect / RadiiX / RadiiY /
// DeviceToLocal (identity when the rrect is device aligned); only the anti-alias form is served
// (ClassifyCoverageFP rejects non-AA RRectEffects). Per-corner order is [TL, TR, BR, BL].
float rrectClipCoverage() {
  // Step 1: local-space coordinate via the device-to-local map (identity is a no-op multiply).
  highp vec3 hl = DeviceToLocal * vec3(gl_FragCoord.xy, 1.0);
  highp vec2 local = hl.xy / hl.z;

  // Step 2: arc-box dispatch. Each corner has a quadrant-local arc box of size radii[i];
  // signsX/Y flip the coordinate so all four queries are first-quadrant.
  highp vec4 cornersX = vec4(LocalRect.x, LocalRect.z, LocalRect.z, LocalRect.x);
  highp vec4 cornersY = vec4(LocalRect.y, LocalRect.y, LocalRect.w, LocalRect.w);
  vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);
  vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);
  highp vec4 dx = (vec4(local.x) - cornersX) * signsX;
  highp vec4 dy = (vec4(local.y) - cornersY) * signsY;
  vec4 inBox = step(dx, RadiiX) * step(dy, RadiiY);
  // One-hot collapse: when adjacent arc boxes share a fragment along their boundary, keep the
  // first hit so the arc-center/radii queries below collapse to a single corner.
  vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
  inBox *= step(prefixSum, vec4(0.5));
  float inAnyCorner = dot(inBox, vec4(1.0));

  // Step 3: select arc center / radii via dot products with the one-hot mask.
  highp vec4 arcCentersX = cornersX + signsX * RadiiX;
  highp vec4 arcCentersY = cornersY + signsY * RadiiY;
  highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
  highp vec2 r = vec2(dot(inBox, RadiiX), dot(inBox, RadiiY));

  // Step 4: half-pixel radius guard — safeRadii is a divisor below; 0.5 is sub-pixel so the
  // corner still looks the same.
  highp vec2 safeRadii = max(r, vec2(0.5));

  // Step 5: ellipse SDF (Sampson distance) inside the corner region.
  highp vec2 offset = (local - arcCenter) / safeRadii;
  highp vec2 safeOffset = max(abs(offset), vec2(1.0 / 4096.0));
  highp float test = dot(safeOffset, safeOffset) - 1.0;
  highp vec2 grad = 2.0 * safeOffset / safeRadii;
  highp float gradDot = max(dot(grad, grad), 1.1755e-38);
  highp float cornerDist = test * inversesqrt(gradDot);

  // Step 6: edge / center distance (negative inside, positive outside).
  highp vec2 d0 = local - LocalRect.xy;
  highp vec2 d1 = LocalRect.zw - local;
  highp float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));

  // Step 7: choose corner SDF or edge SDF based on the arc-box mask.
  highp float dist = mix(edgeDist, cornerDist, inAnyCorner);

  // Step 8: analytic AA coverage — one-pixel ramp centered on the boundary.
  return clamp(0.5 - dist, 0.0, 1.0);
}
