// The sky behind everything: the atmosphere from the sky-view LUT, the sun's
// disc, the moon, the stars and the Milky Way, and the aurora.
in vec2 vUV;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragDist;

uniform sampler2D uSkyView;
uniform mat3  uCamBasis;
uniform vec2  uTanHalf;
uniform float uCamRkm;
uniform vec3  uSunDir, uSunIllum, uMoonDir, uMoonIllum;
uniform float uMoonPhase;
uniform float uStars;          // star brightness (already exposure-scaled)
uniform float uAurora;
uniform float uTime;
uniform float uPixelAngle;

// --- stars: one candidate per cell of a cube-mapped grid -------------------
vec3 stars(vec3 d) {
    vec3 a = abs(d);
    int face;
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0.0 ? 0 : 1; uv = d.zy / a.x; }
    else if (a.y >= a.z)          { face = d.y > 0.0 ? 2 : 3; uv = d.xz / a.y; }
    else                          { face = d.z > 0.0 ? 4 : 5; uv = d.xy / a.z; }
    // Sparse and point-like: a star is a pixel's footprint of light, never
    // larger, and nearly all of them are faint; only a rare few stand out.
    const float CELLS = 520.0;
    vec2 g = (uv * 0.5 + 0.5) * CELLS;
    vec3 sum = vec3(0.0);
    ivec2 base = ivec2(floor(g));
    float px = uPixelAngle * CELLS * 0.5;       // the pixel's size in cells
    float r = max(px * 0.75, 0.02);
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        ivec2 c = base + ivec2(x, y);
        uint h = hash_u(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(face) * 83492791u);
        float pres = float(h & 4095u) / 4095.0;
        if (pres > 0.022) continue;
        vec2 pos = vec2(c) + vec2(hash1(h ^ 0x1234u), hash1(h ^ 0x5678u));
        float mag = pow(hash1(h ^ 0x9abcu), 14.0);          // a steep magnitude law
        float temp = hash1(h ^ 0xdef0u);
        // cold: mostly blue-white, a few pale yellow
        vec3 tint = temp < 0.12 ? vec3(1.0, 0.9, 0.78) : mix(vec3(0.86, 0.92, 1.0), vec3(0.72, 0.82, 1.0), temp);
        float dd = length(g - pos) / r;
        float twinkle = 0.9 + 0.1 * sin(uTime * (2.0 + temp * 4.0) + float(h & 255u));
        // flux spread over the footprint, so its size on screen never grows
        sum += tint * exp(-dd * dd * 2.0) * (0.0025 + mag * 0.35) * twinkle * (0.64 / (r * r * 6.283));
    }
    return sum;
}

float snoise2(vec2 p) {
    vec2 i = floor(p), f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2i(ivec2(i)), hash2i(ivec2(i) + ivec2(1, 0)), u.x),
               mix(hash2i(ivec2(i) + ivec2(0, 1)), hash2i(ivec2(i) + ivec2(1, 1)), u.x), u.y);
}

// Value noise on the sphere of directions: 3D, so it has no seam anywhere.
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = p - i;
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 c = ivec3(i);
    float n000 = hash1(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u);
    float n100 = hash1(uint(c.x + 1) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u);
    float n010 = hash1(uint(c.x) * 73856093u ^ uint(c.y + 1) * 19349663u ^ uint(c.z) * 83492791u);
    float n110 = hash1(uint(c.x + 1) * 73856093u ^ uint(c.y + 1) * 19349663u ^ uint(c.z) * 83492791u);
    float n001 = hash1(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z + 1) * 83492791u);
    float n101 = hash1(uint(c.x + 1) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z + 1) * 83492791u);
    float n011 = hash1(uint(c.x) * 73856093u ^ uint(c.y + 1) * 19349663u ^ uint(c.z + 1) * 83492791u);
    float n111 = hash1(uint(c.x + 1) * 73856093u ^ uint(c.y + 1) * 19349663u ^ uint(c.z + 1) * 83492791u);
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// The Milky Way: a faint band of unresolved stars along a tilted great
// circle - a fine-grained glow, brighter in clumps, split by a dark dust lane
// down its middle. Everything is noise on the direction itself, so nothing
// wraps and there is no seam.
vec3 milky_way(vec3 d) {
    vec3 n = normalize(vec3(0.31, 0.82, -0.48));
    float b = dot(d, n);                                  // across the band
    float band = exp(-b * b * 40.0);
    if (band < 0.01) return vec3(0.0);
    float clumps = vnoise3(d * 6.0) * 0.5 + vnoise3(d * 13.0) * 0.3 + vnoise3(d * 29.0) * 0.2;
    float grain = vnoise3(d * 90.0) * 0.6 + vnoise3(d * 210.0) * 0.4;
    float glow = band * (0.35 + 0.9 * clumps * clumps) * (0.7 + 0.6 * grain);
    // the dust lane: dark, ragged, hugging the centre line
    float lane = exp(-b * b * 900.0) * smoothstep(0.35, 0.75, vnoise3(d * 18.0 + 3.0));
    return vec3(0.78, 0.82, 1.0) * glow * (1.0 - 0.5 * lane);
}

// --- the aurora: curtains hanging 100-300 km up ------------------------------
// Marched as a volume. Each curtain is a thin sheet standing on a folded
// line across the sky, lit from a sharp lower edge near 100 km (green oxygen)
// and fading upward into the red and violet of the thin air above 200 km.
// Along the sheet the light is combed into fine vertical rays, which is what
// makes the eye read it as falling plasma rather than painted bands.
float noise1(float x) { return snoise2(vec2(x, 7.31)); }

vec3 aurora(vec3 d) {
    if (uAurora <= 0.0 || d.y < 0.0) return vec3(0.0);
    float camh = uCamRkm - ATM_BOTTOM;                       // km
    float dy = max(d.y, 0.025);
    const int N = 88;
    const float H0 = 92.0, H1 = 320.0;
    float dh = (H1 - H0) / float(N);
    float ds = dh / dy;                                      // path per step, km
    float jit = ign(gl_FragCoord.xy, 0.0);
    float tm = uTime;
    vec3 col = vec3(0.0);
    for (int i = 0; i < N; ++i) {
        float h = H0 + (float(i) + jit) * dh;
        float dist = (h - camh) / dy;
        vec2 p = d.xz * dist;                                // km, flat-earth is fine up there
        // the rays narrower than a pixel at this distance blend to their mean
        float foot = dist * uPixelAngle;
        float f1 = saturate(1.0 - foot * 0.5), f2 = saturate(1.0 - foot * 1.6);
        float e = 0.0;
        float lowh = 0.0;
        // curtains in rows across the whole sky, one to a 260 km band and a
        // second set between them, each on its own slowly folding line
        for (int k = 0; k < 2; ++k) {
            float zz = p.y + float(k) * 130.0;
            float cell = floor(zz / 260.0);
            float zl = zz - cell * 260.0 - 130.0;
            float fk = hash2i(ivec2(int(cell), k + 3)) * 50.0;
            float lit = hash2i(ivec2(int(cell), k + 11));
            if (lit < 0.3) continue;                         // some rows are dark
            float x = p.x * 0.9 + fk * 37.0;
            float zc = 55.0 * sin(x * 0.0065 + tm * 0.02 + fk)
                     + 40.0 * (noise1(x * 0.012 + tm * 0.03 + fk) - 0.5)
                     + 12.0 * (noise1(x * 0.05 - tm * 0.05 + fk) - 0.5);
            float dz = abs(zl - zc);
            // the sheet is a few km thick, and fades out along its length
            float sheet = exp(-dz * dz / 18.0) * smoothstep(0.25, 0.6, noise1(x * 0.004 + fk + tm * 0.004)) * lit;
            if (sheet < 0.002) continue;
            // its lower edge wanders a little in height, sharp below, soft above
            float base = 100.0 + 14.0 * noise1(x * 0.02 + fk * 7.0);
            float prof = smoothstep(base - 2.0, base + 2.5, h) * exp(-max(h - base, 0.0) / (40.0 + 30.0 * fract(fk)));
            // rays: narrow vertical streaks along the sheet, flickering
            float r1 = noise1(x * 0.45 + tm * 0.4 + fk * 19.0);
            float r2 = noise1(x * 1.3 - tm * 0.9 + fk * 3.0);
            float rays = 0.25 + 0.75 * mix(0.2, pow(r1, 3.0), f1) + 0.35 * mix(0.13, pow(r2, 4.0), f2);
            float s = sheet * prof * rays;
            e += s;
            lowh += s * smoothstep(base + 6.0, base, h);
        }
        // colour by height: violet fringe at the very bottom, green, then
        // red up where the oxygen glows slowly
        vec3 c = mix(vec3(0.12, 1.0, 0.38), vec3(0.85, 0.12, 0.35), smoothstep(150.0, 240.0, h));
        c = mix(c, vec3(0.55, 0.25, 1.0), smoothstep(250.0, 310.0, h) * 0.6);
        col += (c * e + vec3(0.5, 0.2, 0.9) * lowh * 0.35) * ds;
    }
    return col * uAurora * 0.0022 * smoothstep(0.0, 0.08, d.y);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 d = normalize(uCamBasis * vec3(ndc.x * uTanHalf.x, ndc.y * uTanHalf.y, -1.0));
    // level 0 by hand: the azimuth wraps due south, and the jump in the
    // derivatives there would pull the smallest mip - a line across the sky
    vec3 col = textureLod(uSkyView, skyview_uv(d, uCamRkm), 0.0).rgb;

    // what the atmosphere lets through along this line of sight
    vec3 T = transmittance(uCamRkm, d.y);
    vec3 ro = vec3(0.0, uCamRkm, 0.0);
    bool ground = ray_sphere(ro, d, ATM_BOTTOM).x > 0.0;
    if (!ground) {
        // the sun: 0.53 degrees, darker at the limb
        float cs = dot(d, uSunDir);
        float sr = 0.00465;
        float ang = acos(clamp(cs, -1.0, 1.0));
        if (ang < sr * 1.4) {
            float x = saturate(ang / sr);
            float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - x * x, 0.0)));
            float disc = 1.0 - smoothstep(0.92, 1.05, ang / sr);
            vec3 L = uSunIllum / (PI * sr * sr) * limb * disc;
            // clamped: past this the disc is white anyway, and the bloom of an
            // honest sun would swallow half the screen
            col += min(L * T, vec3(2500.0));
        }
        // the moon: a lit sphere, phase from the scenario
        float cm = dot(d, uMoonDir);
        float mr = 0.0052;
        float angm = acos(clamp(cm, -1.0, 1.0));
        if (angm < mr * 1.2 && dot(uMoonIllum, vec3(1.0)) > 0.0) {
            vec3 mz = uMoonDir;
            vec3 mx = normalize(cross(vec3(0.0, 1.0, 0.0), mz));
            vec3 my = cross(mz, mx);
            vec2 q = vec2(dot(d - mz, mx), dot(d - mz, my)) / mr;
            float rr = dot(q, q);
            if (rr < 1.0) {
                vec3 n = vec3(q, sqrt(1.0 - rr));
                float ph = (1.0 - uMoonPhase) * PI;          // terminator angle
                vec3 ldir = vec3(sin(ph), 0.15, cos(ph));
                float lit = saturate(dot(n, normalize(ldir)));
                float maria = 0.75 + 0.25 * snoise2(q * 3.0 + 7.0) - 0.2 * smoothstep(0.55, 0.7, snoise2(q * 1.6 + 2.0));
                vec3 L = uMoonIllum * 30000.0 * lit * maria;
                col = mix(col, col * 0.2, 0.0) + L * T;
            }
        }
        // stars and the Milky Way, dimmed by the air in front of them
        if (uStars > 0.0) {
            vec3 st = stars(d) + milky_way(d) * 0.018;
            col += st * uStars * T;
        }
        col += aurora(d) * T;
    }
    fragColor = vec4(col, 1.0);
    fragDist = vec4(1e7, 0.0, 0.0, 1.0);
}
