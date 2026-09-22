// The terrain's height function. src/terrain.c carries a line-for-line C copy
// (the flight model needs the ground's height for the soft floor and terrain
// following), so any change here has to be made there too.
//
// World x/z in metres, heights in metres above sea level. Everything is
// integer-hashed value noise: a sin() hash falls apart at the half a million
// metres the flight reaches, an integer one does not care.

uniform vec4 uT0;   // base elevation, continent amplitude, continent scale km, hill amplitude
uniform vec4 uT1;   // hill scale km, mountain amplitude, mountain scale km, mountain mask low
uniform vec4 uT2;   // mountain mask high, river strength, river scale km, dune amplitude
uniform vec4 uT3;   // snow line, tree line, biome, seed
uniform vec4 uT4;   // field size km, forest cover, town density, snow cover
uniform vec4 uT5;   // while scenery changes: the biome coming, how far it has come
uniform vec4 uU0, uU1, uU2, uU3, uU4;   // and the coming ground's own uT0..uT4

uint thash(int x, int y, uint seed) {
    uint h = (uint(x) * 0x27d4eb2du) ^ (uint(y) * 0x165667b1u) ^ seed;
    h = (h ^ (h >> 15u)) * 0x85ebca6bu;
    h = (h ^ (h >> 13u)) * 0xc2b2ae35u;
    return h ^ (h >> 16u);
}
float thval(int x, int y, uint s) { return float(thash(x, y, s) >> 8u) * (2.0 / 16777216.0) - 1.0; }

// Value noise in [-1, 1] and its gradient.
vec3 tnoised(vec2 p, uint s) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    int ix = int(i.x), iy = int(i.y);
    float a = thval(ix, iy, s), b = thval(ix + 1, iy, s);
    float c = thval(ix, iy + 1, s), d = thval(ix + 1, iy + 1, s);
    float k1 = b - a, k2 = c - a, k3 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k3 * u.x * u.y,
                du * vec2(k1 + k3 * u.y, k2 + k3 * u.x));
}
float tnoise(vec2 p, uint s) { return tnoised(p, s).x; }

const mat2 TROT = mat2(0.8, 0.6, -0.6, 0.8);

// Plain fbm, a fixed number of octaves, roughly [-1, 1].
float tfbm(vec2 p, int oct, uint s) {
    float sum = 0.0, amp = 0.5;
    for (int i = 0; i < oct; ++i) {
        sum += amp * tnoise(p, s + uint(i) * 7919u);
        p = TROT * p * 2.03;
        amp *= 0.5;
    }
    return sum;
}

// Weight of an octave of the given wavelength when nothing finer than
// minWave (metres) can be seen: 1 well above it, fading to 0 at it.
float toct(float wave, float minWave) { return clamp(wave / minWave - 1.0, 0.0, 1.0); }

// Band-limited fbm: octaves finer than minWave are dropped, smoothly.
float tfbm_lim(vec2 p, float scale_m, int oct, uint s, float minWave) {
    float sum = 0.0, amp = 0.5, wave = scale_m;
    for (int i = 0; i < oct; ++i) {
        float w = toct(wave, minWave);
        if (w <= 0.0) break;
        sum += amp * w * tnoise(p, s + uint(i) * 7919u);
        p = TROT * p * 2.0;
        wave *= 0.5;
        amp *= 0.5;
    }
    return sum;
}

// Mountains: fbm whose octaves are damped where the slope so far is steep,
// which is what gives ridges and valleys that read as eroded rather than as
// noise. Roughly [0, 1].
float tmountain(vec2 p, float scale_m, uint s, float minWave) {
    float sum = 0.0, amp = 0.5, wave = scale_m;
    vec2 d = vec2(0.0);
    for (int i = 0; i < 11; ++i) {
        float w = toct(wave, minWave);
        if (w <= 0.0) break;
        vec3 n = tnoised(p, s + uint(i) * 104729u);
        // ridged: fold the noise so the crests are sharp
        float r = 1.0 - abs(n.x);
        // the fold's slope, with its sign eased across the crest: a hard
        // sign flips the slope there, and every finer octave, eroded by the
        // slope so far, would jump - a step in the ground along each crest,
        // and in high ranges a cliff (the same in terrain.c)
        vec2 g = -(n.x / (abs(n.x) + 0.08)) * n.yz;
        d += g * amp;
        sum += w * amp * r * r / (1.0 + dot(d, d) * 1.4);
        p = TROT * p * 2.0;
        wave *= 0.5;
        amp *= 0.5;
    }
    return sum;
}

float height_one(vec2 xz, float minWave, vec4 T0, vec4 T1, vec4 T2) {
    uint seed = uint(uT3.w);
    float h = T0.x;
    // continents and basins
    float cscale = T0.z * 1000.0;
    h += T0.y * tfbm_lim(xz / cscale, cscale, 6, seed + 11u, minWave);
    // mountain ranges, only where the mask says there is a range
    if (T1.y > 0.0) {
        float mscale = T1.z * 1000.0;
        float mask = smoothstep(T1.w, T2.x, tfbm(xz / (mscale * 5.0) + 17.3, 3, seed + 23u));
        if (mask > 0.0)
            h += T1.y * mask * tmountain(xz / mscale, mscale, seed + 31u, minWave);
    }
    // rolling hills
    float hscale = T1.x * 1000.0;
    h += T0.w * tfbm_lim(xz / hscale + 5.1, hscale, 7, seed + 41u, minWave);
    // dunes: long crests with a steep lee side, bent by the wind
    if (T2.w > 0.0 && minWave < 900.0) {
        vec2 q = xz / 700.0;
        q += 0.9 * vec2(tnoise(q * 0.21, seed + 51u), tnoise(q * 0.21 + 7.7, seed + 52u));
        float crest = abs(tnoise(q * vec2(1.0, 0.33), seed + 53u));
        float dune = pow(1.0 - crest, 3.0) * toct(700.0, minWave);
        h += T2.w * dune * smoothstep(-0.2, 0.4, tnoise(xz / 9000.0, seed + 54u));
    }
    // rivers: the zero lines of a warped noise, cut down to the water where
    // the land is low enough for a river to wander
    if (T2.y > 0.0) {
        float rscale = T2.z * 1000.0;
        vec2 q = xz / rscale;
        q += 0.55 * vec2(tfbm(q * 1.7 + 3.1, 3, seed + 61u), tfbm(q * 1.7 + 9.2, 3, seed + 62u));
        float r = abs(tnoise(q, seed + 63u));
        float width = 0.012 * T2.y;
        float bank = smoothstep(width, width * 5.0, r);
        float low = smoothstep(900.0, 300.0, h);
        h = mix(h, mix(-6.0, h, bank), low);
    }
    return h;
}

// While the scenery changes, the heights of the old ground and the new are
// blended - never their parameters (see terrain.h).
float terrain_height(vec2 xz, float minWave) {
    float h = height_one(xz, minWave, uT0, uT1, uT2);
    if (uT5.y > 0.0) h = mix(h, height_one(xz, minWave, uU0, uU1, uU2), uT5.y);
    return h;
}
