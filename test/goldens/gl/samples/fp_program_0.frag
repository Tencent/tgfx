#version 150
layout(std140) uniform FragmentUniformBlock {
     vec4 Color_P0;
     vec4 leftBorderColor_P1;
     vec4 rightBorderColor_P1;
     vec4 start_P2;
     vec4 end_P2;
};

in vec2 TransformedCoords_0_P0;

out vec4 tgfx_FragColor;

float tgfxLinearGradientT(highp vec2 coord) {
  return coord.x + 1.0000000000000001e-05;
}


vec4 tgfxSingleGradientColor(float t, vec4 start, vec4 end) {
  return (1.0 - t) * start + t * end;
}


void main() {
    vec4 outputColor_P0;
    vec4 outputCoverage_P0;
    { // Processor0 : QuadPerEdgeAAGeometryProcessor
        outputCoverage_P0 = vec4(1.0);
        outputColor_P0 = Color_P0;
    }
    vec4 output_P1;
    { // Processor1 : ClampedGradientEffect
        vec4 _child1_P1;
        {
            // Processor3 : LinearGradientLayout
            _child1_P1 = vec4(tgfxLinearGradientT(TransformedCoords_0_P0), 1.0, 0.0, 0.0);
        }
        vec4 t = _child1_P1;
        if (t.y < 0.0) {
            output_P1 = vec4(0.0);
        } else if (t.x <= 0.0) {
            output_P1 = leftBorderColor_P1;
        } else if (t.x >= 1.0) {
            output_P1 = rightBorderColor_P1;
        } else {
            vec4 _child0_P1;
            vec4 _childInput_P2 = t;
            {
                // Processor2 : SingleIntervalGradientColorizer
                _child0_P1 = tgfxSingleGradientColor(_childInput_P2.x, start_P2, end_P2);
            }
            output_P1 = _child0_P1;
        }
        output_P1.rgb *= output_P1.a;
        output_P1 *= outputColor_P0.a;
    }
    { // Processor4 : EmptyXferProcessor
        tgfx_FragColor = output_P1 * outputCoverage_P0;
    }
}

