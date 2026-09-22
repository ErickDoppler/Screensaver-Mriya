// From light to picture: rain on the lens, the glow, exposure, the eye's
// night vision, a filmic tone curve and flare from the sun. The grain and the
// fade for camera cuts follow in present.frag.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uHDR;
uniform sampler2D uBloom;
uniform sampler2D uAdapt;
uniform sampler2D uProbe;
uniform float uBloomAmt;
uniform float uKey;           // mid-grey target
uniform float uExpBias;       // stops
uniform float uNight;         // 0..1
uniform vec3  uSun;           // screen uv, and 1 when in front of the camera
uniform vec3  uSunLum;        // expected brightness of the unobstructed disc
uniform float uLens;          // lens effects on
uniform float uDrops;         // rain on the lens, 0..1
uniform vec2  uFlow;          // screen direction the water runs
uniform float uTime;
uniform float uFade;
uniform vec2  uRes;

vec3 aces(vec3 x) {
    // Narkowicz fit of the ACES reference tone curve
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
vec3 to_srgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// Water on the lens: drops that bead, then run off in the airflow.
vec2 drops(vec2 uv, out float wet) {
    wet = 0.0;
    if (uDrops <= 0.0) return vec2(0.0);
    vec2 offs = vec2(0.0);
    float aspect = uRes.x / uRes.y;
    for (int layer = 0; layer < 2; ++layer) {
        float sc = layer == 0 ? 9.0 : 17.0;
        vec2 p = uv * vec2(aspect, 1.0) * sc;
        p -= uFlow * uTime * (layer == 0 ? 2.5 : 4.0);
        vec2 cell = floor(p);
        vec2 f = p - cell;
        float h = hash2i(ivec2(cell) + ivec2(layer * 131));
        if (h > uDrops * 0.3) continue;
        vec2 ctr = vec2(0.3 + 0.4 * hash2i(ivec2(cell) + ivec2(7)), 0.3 + 0.4 * hash2i(ivec2(cell) + ivec2(3)));
        float r = 0.12 + 0.18 * hash2i(ivec2(cell) + ivec2(11));
        vec2 d = (f - ctr) / r;
        float l = dot(d, d);
        if (l < 1.0) {
            float z = sqrt(1.0 - l);
            offs += d * (1.0 - z) * 0.006 / sc * 9.0;
            wet = max(wet, smoothstep(1.0, 0.7, l) * 0.6);
        }
    }
    return offs;
}

void main() {
    vec2 uv = vUV;
    float wet;
    vec2 off = drops(uv, wet) * uLens;
    vec3 hdr = texture(uHDR, uv + off).rgb;
    // a drop gathers light from around it, and blurs
    if (wet > 0.0) hdr = mix(hdr, texture(uHDR, uv + off * 2.0, 2.0).rgb * 1.1, wet * 0.5 * uLens);
    hdr += texture(uBloom, uv).rgb * uBloomAmt;

    // sun flare: how much of the disc is really showing decides everything
    if (uLens > 0.0 && uSun.z > 0.0) {
        float vis = 0.0;
        for (int i = 0; i < 5; ++i) {
            vec2 o = vec2(i == 1 ? 1.0 : (i == 2 ? -1.0 : 0.0), i == 3 ? 1.0 : (i == 4 ? -1.0 : 0.0)) * 0.004;
            vis += saturate(luma(texture(uHDR, uSun.xy + o).rgb) / max(luma(uSunLum), 1e-4));
        }
        vis /= 5.0;
        vec2 toc = uv - uSun.xy;
        toc.x *= uRes.x / uRes.y;
        float glare = exp(-length(toc) * 14.0) * 0.06 + exp(-length(toc) * 45.0) * 0.25;
        vec3 flare = uSunLum * glare * vis * 0.0004;
        // ghosts along the line through the centre
        vec2 axis = vec2(0.5) - uSun.xy;
        for (int g = 1; g <= 4; ++g) {
            vec2 gp = uSun.xy + axis * (float(g) * 0.55);
            vec2 dd = uv - gp;
            dd.x *= uRes.x / uRes.y;
            float rad = 0.02 + 0.03 * float(g & 1) + 0.015 * float(g);
            float ring = smoothstep(rad, rad * 0.7, length(dd));
            vec3 tint = g == 1 ? vec3(0.4, 0.6, 1.0) : (g == 2 ? vec3(1.0, 0.7, 0.3) : (g == 3 ? vec3(0.5, 1.0, 0.6) : vec3(0.9, 0.5, 1.0)));
            flare += tint * ring * uSunLum * vis * 0.000006;
        }
        hdr += flare;
    }

    float avg = texelFetch(uAdapt, ivec2(0), 0).r;
    float exposure = uKey / clamp(avg, 0.02, 50.0) * exp2(uExpBias);
    // a night scene should still look like night, however well adapted
    exposure *= mix(1.0, 0.45, uNight);
    vec3 c = hdr * exposure;
    // Purkinje shift: in the dark the eye loses colour and leans blue
    float scot = uNight * (1.0 - smoothstep(0.02, 0.4, luma(c)));
    c = mix(c, vec3(luma(c)) * vec3(0.75, 0.9, 1.25), scot * 0.7);

    c = aces(c * 1.05);
    vec2 d = uv - 0.5;
    c *= 1.0 - dot(d, d) * 0.35 * uLens;
    c = to_srgb(c);
    // (grain and the fade come last, in present.frag)
    frag = vec4(c, 1.0);
}
