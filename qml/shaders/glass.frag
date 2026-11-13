#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    vec4 tint;
    float cornerRadius;
    float density;
    float refraction;
};
layout(binding=1) uniform sampler2D backdrop;
void main() {
    vec2 p = qt_TexCoord0 * resolution;
    float r = min(cornerRadius, min(resolution.x, resolution.y)*0.5);
    vec2 q = abs(p-resolution*0.5)-(resolution*0.5-vec2(r));
    float d = length(max(q,0.0))+min(max(q.x,q.y),0.0)-r;
    float mask = 1.0-smoothstep(-1.0,0.5,d);
    vec2 normal = q.x > q.y ? vec2(sign(p.x-resolution.x*0.5),0.0) : vec2(0.0,sign(p.y-resolution.y*0.5));
    if (q.x > 0.0 && q.y > 0.0)
        normal = normalize(q) * sign(p-resolution*0.5);
    vec2 uv = qt_TexCoord0 + normal * refraction * exp(-abs(d)/7.0)/resolution;
    vec2 stepUV = vec2(8.0)/resolution;
    vec4 b = texture(backdrop, uv)*0.2;
    for (int i=0;i<8;++i) {
        float a=float(i)*0.78539816;
        vec2 v=vec2(cos(a),sin(a))*stepUV;
        b += texture(backdrop,clamp(uv+v,vec2(0.0),vec2(1.0)))*0.075;
        b += texture(backdrop,clamp(uv+v*2.0,vec2(0.0),vec2(1.0)))*0.025;
    }
    vec3 scene = b.rgb + tint.rgb * (1.0-b.a);
    float sceneLight = dot(scene,vec3(0.2126,0.7152,0.0722));
    float tintLight = dot(tint.rgb,vec3(0.2126,0.7152,0.0722));
    float safeDensity = density;
    // Bright covers behind dark glass (and dark covers behind light glass)
    // need a denser neutral material to keep foreground text readable.
    if (tintLight < 0.5 && sceneLight > 0.23)
        safeDensity = max(safeDensity,clamp((sceneLight-0.23)/max(0.001,sceneLight-tintLight),0.0,1.0));
    else if (tintLight >= 0.5 && sceneLight < 0.91)
        safeDensity = max(safeDensity,clamp((0.91-sceneLight)/max(0.001,tintLight-sceneLight),0.0,1.0));
    vec3 base=mix(scene,tint.rgb,safeDensity);
    float edge=exp(-abs(d+1.0)*1.1);
    float light = clamp(dot(normal,normalize(vec2(-0.45,-0.9)))*0.5+0.5,0.0,1.0);
    base += edge * mix(0.012,0.11,light);
    // A wider inner reflection gives the rim thickness, rather than a white outline.
    base += exp(-abs(d+4.0)*0.35) * light * 0.025;
    base *= 1.0 - exp(-abs(d+9.0)*0.18) * (1.0-light) * 0.025;
    float alpha=mask*qt_Opacity;
    fragColor=vec4(base*alpha,alpha);
}
