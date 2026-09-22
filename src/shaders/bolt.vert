// A lightning channel, one segment per instance, expanded to a camera-facing
// ribbon.
layout(location = 0) in vec4 aA;     // xyz start (camera-relative), w width
layout(location = 1) in vec4 aB;     // xyz end, w brightness

uniform mat4 uViewProj;
uniform float uGlow;                 // 1 for the wide glow pass
out vec2 vQuad;
out float vBright;

void main() {
    int corner = gl_VertexID;
    float t = (corner & 1) == 0 ? 0.0 : 1.0;
    float s = (corner & 2) == 0 ? -1.0 : 1.0;
    vec3 p = mix(aA.xyz, aB.xyz, t);
    vec3 side = normalize(cross(aB.xyz - aA.xyz, p));
    float w = aA.w * mix(1.0, 9.0, uGlow);
    gl_Position = uViewProj * vec4(p + side * w * s, 1.0);
    vQuad = vec2(t, s);
    vBright = aB.w;
}
