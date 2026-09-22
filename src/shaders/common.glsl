// Prepended to every shader, after the #version line.
const float PI = 3.14159265358979;
const float EARTH_R = 6360000.0;         // metres, matches the atmosphere's ground

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
float remap(float v, float lo, float hi, float nlo, float nhi) {
    return nlo + (v - lo) / (hi - lo) * (nhi - nlo);
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Interleaved gradient noise: a cheap per-pixel dither that looks like blue
// noise. `frame` scrolls it so the pattern never sits still.
float ign(vec2 px, float frame) {
    px += frame * 5.588238;
    return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

uint hash_u(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}
float hash1(uint x) { return float(hash_u(x) >> 8u) * (1.0 / 16777216.0); }
float hash2i(ivec2 p) { return hash1(uint(p.x) * 1597334677u ^ uint(p.y) * 3812015801u); }

// Ray / sphere, sphere centred at the origin. Returns the two distances
// (x < y), or y < 0 when the ray misses.
vec2 ray_sphere(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return vec2(-1.0, -2.0);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

// Octahedral-free direction <-> equirect helpers for the sky-view LUT live
// in atmosphere.glsl.

// Henyey-Greenstein phase function.
float hg(float cos_t, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_t, 1e-4), 1.5));
}

uniform vec3 uCamWorld;        // the camera in the world, metres (y = altitude)
