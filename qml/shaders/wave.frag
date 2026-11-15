#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float phase;
    float level;
    float bass;
    float beat;
    float discovery;
    float aspect;
    vec4 inkColor;
    float engaged;
};
void main() {
    vec2 p = (qt_TexCoord0 - 0.5) * vec2(aspect, 1.0);
    float r = length(p);
    float a = atan(p.y, p.x);
    float t = phase * 0.55;
    float pulse = bass * 0.065 + beat * 0.045;
    float radius = 0.305 + pulse;
    float energy = level * 0.022 + beat * 0.018;
    float shape = sin(a * 3.0 + t) * (0.014 + bass * 0.020)
                + cos(a * (5.0 + discovery * 2.0) - t * 1.4) * 0.011;
    // Silky, interwoven contours. Sound changes the geometry, not just brightness.
    float ribbons = 0.0;
    float pixel = max(fwidth(r) * 0.65, 0.0001);
    for (int i = 0; i < 7; ++i) {
        float f = float(i);
        float twist = sin(a * 4.0 - t * 1.1 + f * 0.55) * (0.014 + energy)
                    + sin(a * 11.0 + t * 2.5 + f * 0.8) * energy * 0.35;
        float d = abs(r - radius - shape - twist - (f - 3.0) * 0.009);
        float highlight = 0.40 + 0.60 * pow(0.5 + 0.5 * cos(a - t + f * 0.6), 2.0);
        float lineWidth = 0.0021 + f * 0.0002;
        float coverage = sqrt(lineWidth * lineWidth + pixel * pixel);
        ribbons += exp(-d * d / (coverage * coverage)) * (lineWidth / coverage) * highlight * 0.52;
    }
    float haze = exp(-abs(r - radius - shape) * 25.0) * (0.028 + engaged * 0.035);
    float dust = pow(max(0.0, sin(a * 76.0 + sin(a * 7.0) * 2.0)), 18.0);
    float orbit = exp(-abs(r - radius - 0.072 - sin(a * 6.0 + t) * 0.009) * 600.0);
    dust *= orbit * (0.10 + level * 0.22);
    // An expanding, fading pressure front makes individual kicks readable.
    float shock = exp(-abs(r - radius - (1.0 - beat) * 0.095) * 160.0) * beat * 0.18;
    float alpha = clamp(ribbons + haze + dust + shock, 0.0, 0.88);
    alpha *= smoothstep(0.11, 0.21, r) * (1.0 - smoothstep(0.45, 0.50, r));
    fragColor = vec4(inkColor.rgb * alpha, alpha) * qt_Opacity;
}
