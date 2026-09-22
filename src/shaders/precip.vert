// Rain and snow streaming past: drops fixed in the air inside a box that
// travels with the camera (wrapping), each drawn as a streak along its
// velocity relative to the camera over one exposure. At 200 m/s that is
// metres long, which is what rain looks like from an airliner.
uniform mat4  uViewProj;
uniform vec3  uBoxOffset;     // camera position in the air, modulo the box
uniform float uBox;
uniform vec3  uVel;           // drop velocity relative to the camera, m/s
uniform float uStreak;        // exposure, s
uniform float uSnow;          // 0 rain .. 1 snow
uniform float uAmount;        // 0..1
uniform float uPixelAngle;
uniform sampler2D uProbe;

out vec2 vQuad;
out float vAlpha;

void main() {
    uint id = uint(gl_InstanceID);
    int corner = gl_VertexID;          // triangle strip: 0 1 2 3
    vec3 h = vec3(hash1(id * 3u + 1u), hash1(id * 3u + 2u), hash1(id * 3u + 3u));
    vec3 p = mod(h * uBox - uBoxOffset, uBox) - uBox * 0.5;
    float streak = uStreak * mix(1.0, 0.6, uSnow);
    vec3 tail = p - uVel * streak;
    vec3 mid = 0.5 * (p + tail);
    float dist = max(length(mid), 0.05);
    vec3 along = p - tail;
    vec3 side = normalize(cross(along + vec3(1e-4), mid));
    float radius = mix(0.0012, 0.006, uSnow);
    float w = max(radius, dist * uPixelAngle * 0.8);
    float t = (corner & 1) == 0 ? 0.0 : 1.0;
    float s = (corner & 2) == 0 ? -1.0 : 1.0;
    vec3 pos = mix(tail, p, t) + side * w * s;
    gl_Position = uViewProj * vec4(pos, 1.0);
    vQuad = vec2(t, s);
    float present = texelFetch(uProbe, ivec2(0, 0), 0).b * uAmount;
    // thinner than a pixel: spread the same light wider and fainter
    float energy = radius / w;
    vAlpha = present * energy * smoothstep(0.3, 1.5, dist) * smoothstep(uBox * 0.5, uBox * 0.3, dist)
             * step(hash1(id * 7u + 5u), present * 1.2);
}
