// A contrail, one segment per instance, expanded to a ribbon facing the
// camera.
layout(location = 0) in vec4 aA;     // xyz start (camera-relative), w width
layout(location = 1) in vec4 aB;     // xyz end, w opacity
layout(location = 2) in vec2 aAge;   // age at start, age at end (seconds)

uniform mat4 uViewProj;
out vec2 vQuad;
out float vAlpha;
out float vAge;
out vec3 vRel;

void main() {
    int corner = gl_VertexID;
    float t = (corner & 1) == 0 ? 0.0 : 1.0;
    float s = (corner & 2) == 0 ? -1.0 : 1.0;
    vec3 p = mix(aA.xyz, aB.xyz, t);
    vec3 side = normalize(cross(aB.xyz - aA.xyz + vec3(1e-4), p));
    float w = aA.w;
    gl_Position = uViewProj * vec4(p + side * w * s, 1.0);
    vQuad = vec2(t, s);
    vAlpha = aB.w;
    vAge = mix(aAge.x, aAge.y, t);
    vRel = p;
}
