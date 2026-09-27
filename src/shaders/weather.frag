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
uniform vec2  uShearDir;          // the wind: cloud streets line up with it

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

// The mesoscale: a deck is not an even field of cells. It gathers into
// patches and is ribbed by waves tens of kilometres long, which cross and
// interfere - thick where two crests meet, torn open where two troughs do.
// Without this a solid deck is one size of bump repeated to the horizon,
// which is what gives it away.
//
// Five trains, from 5 km to 70 km, each on its own heading and each bent by
// a slow noise so its crests wander and break rather than ruling the sky in
// straight lines.
float wave_field(vec2 air) {
    float s = 0.0, wsum = 0.0;
    for (int i = 0; i < 7; ++i) {
        float fi = float(i);
        uint sd = uint(i) * 7919u + uint(uSeedW * 13.0) + 31u;
        // no two trains alike: wavelengths from about 4 to 70 km, each
        // jittered off the ladder so they do not beat into a regular pattern
        float lam = 4200.0 * pow(1.52, fi) * (0.72 + 0.62 * hash1(sd + 3u));
        float ang = hash1(sd) * 6.2831853;
        vec2 d = vec2(cos(ang), sin(ang));
        // each train wanders - its crests bend and break - and comes and goes
        // over tens of kilometres, so none of them rules the whole sky and
        // nothing repeats
        float bend = (wnoise(air / (lam * 1.4) + fi * 3.7 + uSeedW) - 0.5) * 3.6;
        float amp = wnoise(air / (lam * 6.0) + fi * 11.3 + uSeedW * 0.3);
        amp *= amp;
        float ph = dot(air, d) / lam + bend;
        float w = (0.25 + 1.5 * amp) / (0.8 + fi * 0.28);
        s += w * (0.5 + 0.5 * sin(ph * 6.2831853));
        wsum += w;
    }
    return s / max(wsum, 1e-3);
}

float meso(vec2 air, float salt) {
    float patches = saturate(wfbm(air / 26000.0 + salt + uSeedW * 0.7) * 1.5 - 0.25);
    float waves = wave_field(air + salt * 1000.0);
    return saturate(mix(patches, waves, 0.6) * 1.3 - 0.15);
}

float coverage(vec2 air, float cover, float type, float scale_km, float salt) {
    if (cover <= 0.0) return 0.0;
    float s = scale_km * 1000.0;
    vec2 p = air / (s * 4.0) + salt + uSeedW;
    float n = wfbm(p);
    // clusters: the global cover shifts where the field is cut
    float spread = 0.22 + 0.25 * (1.0 - abs(cover * 2.0 - 1.0));
    float c = saturate(remap(n, 1.0 - cover - spread * 0.5, 1.0 - cover + spread * 0.5, 0.0, 1.0));
    c = mix(c, 1.0, smoothstep(0.85, 1.0, cover));
    // even an overcast deck thins in patches and tears open here and there
    c = saturate(c * (0.42 + 0.95 * meso(air, salt * 0.7 + 3.0)));
    // Heaped cloud stays a field of separate clouds. Where the map saturates
    // the shape noise fills every gap between neighbouring cells and they
    // run together into one mass tens of kilometres across, which is not
    // what a fair-weather sky looks like.
    float conv = smoothstep(0.35, 0.7, type);
    c = min(c, mix(1.0, 0.58, conv));
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
    // How tall each cloud grows: cloud by cloud, and over the mesoscale too,
    // so whole stretches of the deck stand higher than their neighbours.
    float height = saturate(wfbm(air / (uLow1.y * 1000.0 * 2.5) + 91.0 + uSeedW) * 1.6 - 0.25);
    height = mix(height, meso(air, 17.0), 0.45);
    // Heaped cloud stands taller where it is thickest - but a flat layer
    // does not, and holding its top up was what made an overcast deck a
    // level plain with no relief at all.
    float conv = smoothstep(0.3, 0.7, uLow0.w);
    height = max(height, smoothstep(0.55, 1.0, low) * mix(0.12, 0.75, conv));
    // rain under the densest parts
    float rain = uRain * smoothstep(0.45, 0.85, low) * mix(0.6, 1.0, wnoise(air / 3000.0 + uSeedW));
    frag = vec4(low, height, mid, rain);
}
