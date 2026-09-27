// Contrail: ice crystals, brightest looking toward the sun (they scatter
// strongly forward), greyer on the shadowed side.
//
// What makes one read as real is its structure. The exhaust leaves the nozzle
// as a thin dense core; the wing's vortex pair rolls it up and the shear
// breaks it into a row of billows, so the plume is lumpy along its length and
// torn along its edges, coarser as it ages and spreads. It is all keyed to
// the age of the smoke, which travels with it, so the pattern sits still in
// the air instead of sliding along the trail, and to the engine, so the six
// plumes break up differently.
in float vAcross;
in float vAlpha;
in float vAge;
in float vSeed;
in float vWidth;
in vec3  vRel;
out vec4 frag;

uniform sampler2D uProbe;
uniform vec3 uLightDir;
uniform float uTime;

float vh(int a, int b, uint s) { return hash1(uint(a) * 374761393u ^ uint(b) * 668265263u ^ s); }

// value noise in (along the trail, across it)
float n2t(vec2 p, uint s) {
    vec2 i = floor(p), f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    int x = int(i.x), y = int(i.y);
    return mix(mix(vh(x, y, s), vh(x + 1, y, s), f.x),
               mix(vh(x, y + 1, s), vh(x + 1, y + 1, s), f.x), f.y);
}

void main() {
    uint s = uint(vSeed) * 9781u + 17u;
    float across = vAcross;
    float old = clamp(vAge / 30.0, 0.0, 1.0);        // how far it has rolled up

    // The billows: coarse lumps along the trail, finer ones over them. The
    // pattern is stretched as the plume widens, so the lumps grow with it
    // rather than staying a fixed size on screen.
    float u = vAge * 1.6;
    vec2 q = vec2(u, across * 1.3);
    float fl = n2t(q, s) * 0.45 + n2t(q * 2.3 + 11.0, s + 7u) * 0.3 +
               n2t(q * 5.1 + 3.0, s + 23u) * 0.15 + n2t(q * 11.0 + 7.0, s + 51u) * 0.1;
    // torn edges: the width itself wanders, differently on the two sides
    float edge = 0.62 + 0.55 * n2t(vec2(u * 0.7, across * 0.5 + 30.0), s + 41u);
    float r = abs(across) / mix(1.0, edge, 0.85 * old);

    // A soft round cross-section eaten into by the billows: the plume is
    // thickest where a lump is and nearly clear between them, which is what
    // makes it read as cotton rather than as an airbrushed band. The lumps
    // bite deeper toward the edges, so the rope has a solid spine with torn
    // cotton hanging off it.
    float body = 1.0 - smoothstep(0.1, 1.0, r);
    float core = exp(-r * r * 3.0) * mix(1.0, 0.5, old);
    float bite = mix(0.35, 1.0, smoothstep(0.15, 0.95, r));   // deeper out here
    float fluff = mix(1.0, smoothstep(0.18, 0.78, fl), bite * mix(0.35, 1.0, old));
    float a = vAlpha * (body * 0.8 + core * 0.55) * fluff;
    // the first seconds are a tight bright thread
    a *= mix(1.5, 1.0, clamp(vAge / 8.0, 0.0, 1.0));
    a = clamp(a, 0.0, 1.0);
    if (a < 0.002) discard;

    vec3 V = normalize(-vRel);
    float c = dot(-V, uLightDir);
    vec3 key = texelFetch(uProbe, ivec2(3, 0), 0).rgb * texelFetch(uProbe, ivec2(0, 0), 0).r;
    vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb;
    // lumps facing the sun are brighter than the hollows between them, and
    // the sunward edge of the rope brighter than the shaded one
    float lit = 0.8 + 0.45 * fl + 0.15 * across * sign(dot(uLightDir, vec3(1.0, 0.0, 0.0)));
    vec3 col = key * (hg(c, 0.7) * 0.6 + 0.08) * lit + amb * 0.9;
    frag = vec4(col * a, a);
}
