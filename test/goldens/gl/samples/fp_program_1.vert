#version 150
layout(std140) uniform VertexUniformBlock {
     vec4 tgfx_RTAdjust;
};

in vec2 aPosition;
in float inCoverage;

out float Coverage_P0;

void main() {
    Coverage_P0 = inCoverage;
    gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0, 1);
}

