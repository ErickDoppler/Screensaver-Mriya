// One puff of a contrail: ice crystals, brightest looking toward the sun
// (they scatter strongly forward), and shaded from the sun side so the rope
// of puffs reads as rounded lumps rather than a flat band.
in vec2  vQuad;
in float vAlpha;
in float vSeed;
in float vAge;
in vec2  vSunXY;
in vec3  vRel;
out vec4 frag;

uniform sampler2D uProbe;
uniform vec3 uLightDir;

float vh(int a, int b, uint s) { return hash1(uint(a) * 374761393u ^ uint(b) * 668265263u ^ s); }

float n2p(vec2 p, uint s) {
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    int x = int(i.x), y = int(i.y);
    return mix(mix(vh(x, y, s), vh(x + 1, y, s), f.x),
               mix(vh(x, y + 1, s), vh(x + 1, y + 1, s), f.x), f.y);
}

void main() {
    uint s = uint(vSeed) * 2654435761u + 11u;
    // A ragged ball: the edge is eaten into by noise, so no puff is a disc
    // and no two are alike. Older smoke has torn further.
    float old = clamp(vAge / 35.0, 0.0, 1.0);
    vec2 q = vQuad;
    float r = length(q);
    float ang = atan(q.y, q.x);
    vec2 np = vec2(cos(ang), sin(ang)) * 1.7 + q * 0.8;
    float tear = n2p(np + 3.0, s) * 0.6 + n2p(np * 2.7 + 9.0, s + 17u) * 0.4;
    float edge = mix(0.95, 0.55 + 0.7 * tear, mix(0.45, 0.95, old));
    float body = 1.0 - smoothstep(edge * 0.35, edge, r);
    if (body <= 0.0) discard;
    // lumpy inside as well, so overlapping puffs do not average into a wash
    float grain = n2p(q * 2.3 + 17.0, s + 31u) * 0.6 + n2p(q * 5.1 + 5.0, s + 53u) * 0.4;
    float a = vAlpha * body * (0.55 + 0.9 * grain);
    if (a < 0.002) discard;

    // Shading: the side toward the sun is bright, the far side falls into the
    // puff's own shadow - what makes cotton look like cotton. The lit edge is
    // brighter still, where the light grazes through the thin crystals.
    float lam = dot(normalize(q + vSunXY * 1e-3), vSunXY);
    float shade = 0.42 + 0.58 * (0.5 + 0.5 * lam);
    float rim = smoothstep(0.55, 1.0, r) * smoothstep(0.1, 0.9, lam) * 0.5;

    vec3 V = normalize(-vRel);
    float c = dot(-V, uLightDir);
    vec3 key = texelFetch(uProbe, ivec2(3, 0), 0).rgb * texelFetch(uProbe, ivec2(0, 0), 0).r;
    vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb;
    vec3 col = key * (hg(c, 0.7) * 0.6 + 0.1) * (shade + rim) + amb * (0.55 + 0.45 * shade);
    frag = vec4(col * a, a);
}
