#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    vec4 themeColor;
    float time;
    float energy;
    float bass;
    float beat;
    float darkMode;
};

float softUnion(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 p = (uv - vec2(0.5, 0.44)) * vec2(resolution.x / resolution.y, 1.0);
    float t = time * 0.36;
    float expansion = 1.22 + bass * 0.13 + beat * 0.065;
    p /= expansion;
    // Low-frequency domain warping keeps the silhouette round and viscous.
    vec2 warp = vec2(sin(p.y * 7.0 + t * 1.4), cos(p.x * 6.0 - t));
    p += warp * (0.018 + energy * 0.008);
    float field = 1.0;
    float inner = 1.0;
    float folds = 0.0;
    for (int i = 0; i < 6; ++i) {
        float f = float(i);
        float angle = f * 1.0472 + t * (0.23 + f * 0.025);
        vec2 center = vec2(cos(angle + sin(t * 0.6 + f) * 0.7), sin(angle))
                    * vec2(0.16 + 0.035 * sin(t + f), 0.115);
        float radius = 0.155 + 0.028 * sin(t * 1.5 + f * 2.3);
        float d = length(p - center) - radius;
        field = softUnion(field, d, 0.13);
        folds += exp(-abs(d + 0.025) * 38.0) * (0.5 + 0.5 * sin(f * 2.0 + t)) * 0.18;
        inner = softUnion(inner, length(p - center * 0.72) - radius * 0.72, 0.08);
    }
    vec3 hue = themeColor.rgb / max(max(themeColor.r, themeColor.g), max(themeColor.b, 0.01));
    vec3 deep = mix(mix(hue, vec3(1.0), 0.28), hue * 0.34, darkMode);
    vec3 bright = mix(hue, vec3(1.0), 0.32);
    float halo = exp(-max(field, 0.0) * 9.0);
    float body = 1.0 - smoothstep(-0.055, 0.075, field);
    float core = 1.0 - smoothstep(-0.10, 0.09, inner);
    float light = 0.5 + 0.5 * sin(p.x * 6.0 + p.y * 5.0 + t * 1.3);
    vec3 cloud = mix(deep, hue * 0.9, light * 0.75 + 0.13);
    cloud = mix(cloud, bright, clamp(core * (0.30 + energy * 0.15) + folds, 0.0, 0.72));
    vec3 haloColor = mix(mix(hue, vec3(1.0), 0.4), hue * 0.45, darkMode);
    vec3 color = mix(haloColor, cloud, body);
    // Broad pearlescent bands, not wire outlines.
    float rim = exp(-abs(field + 0.018) * 60.0) * (0.5 + 0.5 * sin(p.y * 8.0 - t));
    color += bright * rim * (0.13 + beat * 0.12);
    color += bright * exp(-abs(inner + 0.012) * 48.0) * 0.075;
    // Fade the glow into the page itself, with no opaque card or hard clipping edge.
    vec2 edgeFade = smoothstep(vec2(0.0), vec2(0.10, 0.08), uv)
                  * smoothstep(vec2(0.0), vec2(0.10, 0.08), vec2(1.0) - uv);
    float alpha = max(body * 0.98, halo * 0.48) * edgeFade.x * edgeFade.y * qt_Opacity;
    fragColor = vec4(color * alpha, alpha);
}
