// The weather map: a square of the sky around the aircraft, fixed to the
// moving air mass, saying how much of it the clouds may fill, how tall each
// tower gets, and where it rains. The 3D noise does the shapes; this decides
// where there is anything at all, so a 40% sky has clusters and clearings
// instead of an even sprinkle.
in vec2 vUV;
out vec4 frag;

uniform vec2  uWMCenter;
uniform float uWMSize;
uniform vec4  uLow0, uLow1, uMid0, uMid1;
uniform float uRain;
uniform float uSeedW;

float wnoise(vec2 p) {
    vec2 i = floor(p), f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash2i(ivec2(i)), b = hash2i(ivec2(i) + ivec2(1, 0));
    float c = hash2i(ivec2(i) + ivec2(0, 1)), d = hash2i(ivec2(i) + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float wfbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { s += a * wnoise(p); p = mat2(0.8, 0.6, -0.6, 0.8) * p * 2.02; a *= 0.5; }
    return s;   // ~0..1
}
// distance to the nearest cell centre, for storm cells
float cells(vec2 p) {
    vec2 i = floor(p), f = p - i;
    float d = 9.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec2 o = vec2(x, y);
        vec2 c = i + o;
        vec2 h = vec2(hash2i(ivec2(c)), hash2i(ivec2(c) + ivec2(71, 13)));
        // not every cell holds a storm
        float alive = step(0.35, hash2i(ivec2(c) + ivec2(5, 9)));
        d = min(d, length(o + h - f) + (1.0 - alive) * 9.0);
    }
    return d;
}

float coverage(vec2 air, float cover, float type, float scale_km, float salt) {
    if (cover <= 0.0) return 0.0;
    float s = scale_km * 1000.0;
    vec2 p = air / (s * 7.0) + salt + uSeedW;
    float n = wfbm(p);
    // clusters: the global cover shifts where the field is cut
    float spread = 0.22 + 0.25 * (1.0 - abs(cover * 2.0 - 1.0));
    float c = saturate(remap(n, 1.0 - cover - spread * 0.5, 1.0 - cover + spread * 0.5, 0.0, 1.0));
    c = mix(c, 1.0, smoothstep(0.85, 1.0, cover));
    if (type > 0.75) {
        // storms come as separate cells tens of kilometres apart
        float cellc = 1.0 - smoothstep(0.15, 0.55, cells(air / (s * 4.0) + salt));
        c = mix(c, max(cellc, c * 0.35), saturate((type - 0.75) * 4.0));
    }
    return c;
}

void main() {
    vec2 air = uWMCenter + (vUV - 0.5) * uWMSize;
    float low = coverage(air, uLow0.z, uLow0.w, uLow1.y, 0.0);
    float mid = coverage(air, uMid0.z, uMid0.w, uMid1.y, 37.0);
    // tower height varies cloud by cloud
    float height = saturate(wfbm(air / (uLow1.y * 1000.0 * 2.5) + 91.0 + uSeedW) * 1.6 - 0.25);
    height = max(height, smoothstep(0.4, 1.0, low) * 0.8);
    // rain under the densest parts
    float rain = uRain * smoothstep(0.45, 0.85, low) * mix(0.6, 1.0, wnoise(air / 3000.0 + uSeedW));
    frag = vec4(low, height, mid, rain);
}
