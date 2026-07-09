/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace tgfx {

// Shader header: uniform block + precision + out declaration
static constexpr char GLASS_SHADER_HEADER[] = R"(
    precision highp float;
    in vec2 vTexCoord;
    uniform sampler2D uSource;
    layout(std140) uniform GlassUniforms {
        vec4 uParams0;  // glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
        vec4 uParams1;  // halfW, halfH, cornerRadius, minHalf
        vec4 uParams2;  // innerHalfW, innerHalfH, innerRadius, glassThickness
        vec4 uParams3;  // refractionFactor, dispersion, 1/sourceWidth, 1/sourceHeight
        vec4 uParams4;  // splay, depthRatio, lightAngleRad, lightIntensity
    };
    out vec4 tgfx_FragColor;
)";

// SDF module: Rounded Rectangle
static constexpr char GLASS_SDF_ROUNDED_RECT[] = R"(
    float outerShapeSDF(float px, float py) {
        float hw = uParams1.x;
        float hh = uParams1.y;
        float r = uParams1.z;
        float qx = abs(px) - hw + r;
        float qy = abs(py) - hh + r;
        float outsideDist = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
        float insideDist = min(max(qx, qy), 0.0);
        return outsideDist + insideDist - r;
    }

    float innerShapeSDF(float px, float py) {
        float hw = uParams2.x;
        float hh = uParams2.y;
        float r = uParams2.z;
        float qx = abs(px) - hw + r;
        float qy = abs(py) - hh + r;
        float outsideDist = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
        float insideDist = min(max(qx, qy), 0.0);
        return outsideDist + insideDist - r;
    }

    vec2 shapeNormal(float px, float py) {
        float eps = 0.5;
        float dx = outerShapeSDF(px + eps, py) - outerShapeSDF(px - eps, py);
        float dy = outerShapeSDF(px, py + eps) - outerShapeSDF(px, py - eps);
        vec2 g = vec2(dx, dy);
        float len = length(g);
        if (len > 0.0001) {
            return g / len;
        }
        return vec2(0.0);
    }
)";

// SDF module: Ellipse
static constexpr char GLASS_SDF_ELLIPSE[] = R"(
    float outerShapeSDF(float px, float py) {
        float hw = uParams1.x;
        float hh = uParams1.y;
        float nx = px / hw;
        float ny = py / hh;
        float d = length(vec2(nx, ny)) - 1.0;
        return d * min(hw, hh);
    }

    float innerShapeSDF(float px, float py) {
        float hw = uParams2.x;
        float hh = uParams2.y;
        float nx = px / hw;
        float ny = py / hh;
        float d = length(vec2(nx, ny)) - 1.0;
        return d * min(hw, hh);
    }

    vec2 shapeNormal(float px, float py) {
        float hw = uParams1.x;
        float hh = uParams1.y;
        vec2 n = vec2(px / (hw * hw), py / (hh * hh));
        float len = length(n);
        if (len > 0.0001) {
            return -n / len;
        }
        return vec2(0.0);
    }
)";

// SDF module: Alpha Mask (arbitrary shape via Gaussian-blurred UDF height map)
// The mask texture stores the blurred alpha packed into RGBA8 for 32-bit precision.
// Unpack with: dot(rgba, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0))
static constexpr char GLASS_SDF_ALPHA_MASK[] = R"(
    uniform sampler2D uMask;

    const vec4 UNPACK_FACTORS = vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0);

    vec2 maskPixelToUV(float px, float py) {
        float halfW = uParams1.x;
        float halfH = uParams1.y;
        return vec2((px + halfW) / (halfW * 2.0), 1.0 - (py + halfH) / (halfH * 2.0));
    }

    float sampleHeight(float px, float py) {
        vec2 uv = maskPixelToUV(px, py);
        vec4 packed = texture(uMask, uv);
        return dot(packed, UNPACK_FACTORS);
    }

    // SDF is negative inside the shape and positive outside.
    // Use a low threshold (0.01) since blurred alpha at star tips can be very small.
    float outerShapeSDF(float px, float py) {
        return 0.01 - sampleHeight(px, py);
    }

    float innerShapeSDF(float px, float py) {
        float innerHalfW = uParams2.x;
        float innerHalfH = uParams2.y;
        float halfW = uParams1.x;
        float halfH = uParams1.y;
        float scaleX = innerHalfW / halfW;
        float scaleY = innerHalfH / halfH;
        return 0.01 - sampleHeight(px / scaleX, py / scaleY);
    }

    vec2 shapeNormal(float px, float py) {
        float left = sampleHeight(px - 1.0, py);
        float right = sampleHeight(px + 1.0, py);
        float bottom = sampleHeight(px, py - 1.0);
        float top = sampleHeight(px, py + 1.0);
        vec2 grad = vec2(right - left, top - bottom) * 0.5;
        float len = length(grad);
        if (len > 0.0001) {
            return grad / len;
        }
        return vec2(0.0);
    }

    float shapeGradientMag(float px, float py) {
        float left = sampleHeight(px - 1.0, py);
        float right = sampleHeight(px + 1.0, py);
        float bottom = sampleHeight(px, py - 1.0);
        float top = sampleHeight(px, py + 1.0);
        vec2 grad = vec2(right - left, top - bottom) * 0.5;
        return length(grad);
    }
)";
static constexpr char GLASS_SHADER_MAIN[] = R"(
    void main() {
        float halfW = uParams1.x;
        float halfH = uParams1.y;
        float minHalf = uParams1.w;
        float glassThickness = uParams2.w;
        float refractionFactor = uParams3.x;
        float dispersion = uParams3.y;
        float invSourceW = uParams3.z;
        float invSourceH = uParams3.w;
        float splay = uParams4.x;
        float depthRatio = uParams4.y;

        // Convert source UV to glass pixel coordinates centered at origin.
        vec2 glassUV = (vTexCoord - uParams0.xy) * uParams0.zw;
        glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
        vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
        float px = glassPixel.x - halfW;
        float py = glassPixel.y - halfH;

        vec2 uvOffset = vec2(0.0);

        float outerSDF = outerShapeSDF(px, py);
        float innerSDF = innerShapeSDF(px, py);

        // Apply refraction inside the shape.
        if (outerSDF < 0.0) {
#ifdef GLASS_USE_AXIS_MIX
            // Analytical SDF: use edge band between outer and inner.
            if (innerSDF >= 0.0) {
                float edgeDist = -outerSDF;
                float totalDist = edgeDist + innerSDF;
                float xNorm = (totalDist > 0.001) ? edgeDist / totalDist : 0.0;
                xNorm = min(xNorm, 1.0);
                float edgeFactor = 1.0 - xNorm;
                float offsetDist = glassThickness * refractionFactor * edgeFactor * edgeFactor * 2.0;

                // Refraction direction: mix radial and axis direction based on alignment.
                float dirLen = sqrt(px * px + py * py);
                if (dirLen > 0.001) {
                    vec2 radialDir = vec2(-px / dirLen, -py / dirLen);

                    vec2 axisDir;
                    if (abs(radialDir.x) > abs(radialDir.y)) {
                        axisDir = vec2(sign(radialDir.x), 0.0);
                    } else {
                        axisDir = vec2(0.0, sign(radialDir.y));
                    }

                    float maxAxisDot = max(abs(radialDir.x), abs(radialDir.y));
                    float controlValue = depthRatio * splay;
                    float axisThreshold = mix(0.9, 1.0, controlValue);
                    float axisWeight = smoothstep(axisThreshold, 1.0, maxAxisDot);

                    vec2 refractDir = mix(radialDir, axisDir, axisWeight);
                    float dispX = refractDir.x * offsetDist;
                    float dispY = refractDir.y * offsetDist;
                    uvOffset = vec2(dispX * invSourceW, -dispY * invSourceH);
                }
            }
#else
            // AlphaMask UDF: Figma-style refraction pipeline.
            // Use height to determine edge proximity: height≈0 at edge, height≈max at center.
            float height = sampleHeight(px, py);

            // Step 2: Sobel operator for smoother gradient direction.
            float step = minHalf / 20.0 + 1.0;
            float tl = sampleHeight(px - step, py + step);
            float tc = sampleHeight(px, py + step);
            float tr = sampleHeight(px + step, py + step);
            float ml = sampleHeight(px - step, py);
            float mr = sampleHeight(px + step, py);
            float bl = sampleHeight(px - step, py - step);
            float bc = sampleHeight(px, py - step);
            float br = sampleHeight(px + step, py - step);
            // Sobel X: [-1,0,1; -2,0,2; -1,0,1]
            float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
            // Sobel Y: [-1,-2,-1; 0,0,0; 1,2,1]
            float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
            vec2 grad = vec2(gx, gy) / (8.0 * step);
            float gradLen = length(grad);

            if (gradLen > 0.0001) {
                vec2 gradDir = grad / gradLen;

                // Step 3: Splay mixing (gradient direction vs center direction).
                float dirLen = sqrt(px * px + py * py);
                vec2 centerDir = (dirLen > 0.001) ? vec2(-px / dirLen, -py / dirLen) : vec2(0.0);
                vec2 mixedDir = mix(gradDir, centerDir, splay);
                mixedDir = normalize(mixedDir);

                // Step 4: Offset distance based on edge proximity (1 - height).
                // height≈0 at edge → full strength, height≈max at center → 0.
                float refDist = halfW * (0.5f * refractionFactor + 0.5f * depthRatio);
                float edgeProximity = 1.0 - height;
                float offsetDist = refDist * edgeProximity;

                vec2 sn = mixedDir * offsetDist;
                vec2 sk = vec2(0.999) * refDist;
                sn = clamp(sn, -sk, sk);

                uvOffset = vec2(sn.x * invSourceW, -sn.y * invSourceH);
            }
#endif
        }

        vec2 uvR, uvG, uvB;
        if (dispersion < 0.01) {
            uvR = uvG = uvB = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
        } else {
            uvR = clamp(vTexCoord + uvOffset * (1.0 + dispersion), vec2(0.0), vec2(1.0));
            uvG = clamp(vTexCoord + uvOffset, vec2(0.0), vec2(1.0));
            uvB = clamp(vTexCoord + uvOffset * (1.0 - dispersion), vec2(0.0), vec2(1.0));
        }
        float r = texture(uSource, uvR).r;
        float g = texture(uSource, uvG).g;
        float b = texture(uSource, uvB).b;
        float a = texture(uSource, uvG).a;
        tgfx_FragColor = vec4(r, g, b, a);
    }
)";

}  // namespace tgfx
