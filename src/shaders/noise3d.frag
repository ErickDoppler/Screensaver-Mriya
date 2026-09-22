// Tileable 3D noise for the clouds, rendered one slice at a time into a 3D
// texture at start-up (after Schneider, "The Real-time Volumetric Cloudscapes
// of Horizon Zero Dawn", 2015).
//   mode 0 - the shape volume, 128^3: R Perlin-Worley, GBA Worley fbm at
//            rising frequencies.
//   mode 1 - the detail volume, 32^3: RGB Worley fbm at rising frequencies.
in vec2 vUV;
out vec4 frag;

uniform float uZ;       // slice position, 0..1
uniform int   uMode;

vec3 hash33(ivec3 p) {
    uint x = uint(p.x) * 1597334677u ^ uint(p.y) * 3812015801u ^ uint(p.z) * 2798796415u;
    return vec3(hash1(x), hash1(x ^ 0x68bc21ebu), hash1(x ^ 0x02e5be93u));
}

// Worley (cellular) noise, tiling every `period` cells: 1 at feature points.
float worley(vec3 p, float period) {
    vec3 cell = floor(p);
    vec3 f = p - cell;
    float d = 1e9;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec3 o = vec3(x, y, z);
        vec3 c = mod(cell + o, period);
        vec3 fp = o + hash33(ivec3(c)) - f;
        d = min(d, dot(fp, fp));
    }
    return 1.0 - sqrt(d);
}

// Gradient (Perlin) noise, tiling every `period` cells.
vec3 grad3(vec3 c, float period) {
    c = mod(c, period);
    vec3 h = hash33(ivec3(c)) * 2.0 - 1.0;
    return normalize(h + 1e-5);
}
float perlin(vec3 p, float period) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float n000 = dot(grad3(i + vec3(0, 0, 0), period), f - vec3(0, 0, 0));
    float n100 = dot(grad3(i + vec3(1, 0, 0), period), f - vec3(1, 0, 0));
    float n010 = dot(grad3(i + vec3(0, 1, 0), period), f - vec3(0, 1, 0));
    float n110 = dot(grad3(i + vec3(1, 1, 0), period), f - vec3(1, 1, 0));
    float n001 = dot(grad3(i + vec3(0, 0, 1), period), f - vec3(0, 0, 1));
    float n101 = dot(grad3(i + vec3(1, 0, 1), period), f - vec3(1, 0, 1));
    float n011 = dot(grad3(i + vec3(0, 1, 1), period), f - vec3(0, 1, 1));
    float n111 = dot(grad3(i + vec3(1, 1, 1), period), f - vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float perlin_fbm(vec3 p, float period, int oct) {
    float s = 0.0, a = 1.0, n = 0.0;
    for (int i = 0; i < oct; ++i) {
        s += a * perlin(p, period);
        n += a;
        p *= 2.0;
        period *= 2.0;
        a *= 0.5;
    }
    return s / n;
}

float worley_fbm(vec3 p, float period) {
    return worley(p, period) * 0.625 + worley(p * 2.0, period * 2.0) * 0.25 +
           worley(p * 4.0, period * 4.0) * 0.125;
}

void main() {
    vec3 p = vec3(vUV, uZ);
    if (uMode == 0) {
        float pf = perlin_fbm(p * 4.0, 4.0, 6) * 0.5 + 0.5;
        pf = saturate(remap(pf, 0.1, 0.95, 0.0, 1.0));
        float w0 = worley_fbm(p * 4.0, 4.0);
        // Perlin-Worley: billowy Worley cells carved into the Perlin field
        float pw = saturate(remap(pf, w0 - 1.0, 1.0, 0.0, 1.0));
        frag = vec4(pw, w0, worley_fbm(p * 8.0, 8.0), worley_fbm(p * 16.0, 16.0));
    } else {
        frag = vec4(worley_fbm(p * 4.0, 4.0), worley_fbm(p * 8.0, 8.0),
                    worley_fbm(p * 16.0, 16.0), 1.0);
    }
}
