// Puts the clouds over the scene: the low-resolution cloud image is brought
// up to full size with weights that respect depth, so the aircraft's outline
// does not grow a halo of missing cloud. Then the passage (the cloud bank a
// scenario change flies through) and the screen-wide glow of a close flash.
in vec2 vUV;
out vec4 frag;

uniform sampler2D uScene;
uniform sampler2D uDistTex;
uniform sampler2D uClouds;
uniform sampler2D uCloudDepth;     // each cloud texel's light-weighted distance
uniform sampler2D uProbe;
uniform vec2  uCloudRes;
uniform vec2  uFullRes;
uniform float uPassage;
uniform float uTime;
uniform float uFlashScreen;
uniform mat3  uCamBasis;
uniform vec2  uTanHalf;
uniform vec3  uPlume[6];        // engine nozzles, camera-relative
uniform vec3  uPlumeDir;        // aft, along the exhaust
uniform float uHaze;            // how hard the hot exhaust shimmers

// Heat shimmer: where the line of sight crosses a hot exhaust plume in front
// of what is behind it, the picture wobbles. Each plume is a cone 50 m long.
vec2 exhaust_shimmer(vec2 uv, float scene_t) {
    if (uHaze <= 0.0) return vec2(0.0);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rd = normalize(uCamBasis * vec3(ndc.x * uTanHalf.x, ndc.y * uTanHalf.y, -1.0));
    float s = 0.0;
    for (int i = 0; i < 6; ++i) {
        vec3 a = uPlume[i];
        // closest approach of the ray and the plume axis
        vec3 w0 = -a;
        float b = dot(rd, uPlumeDir);
        float d = dot(rd, w0), e = dot(uPlumeDir, w0);
        float den = 1.0 - b * b;
        if (den < 1e-4) continue;
        float tr = (b * e - d) / den;       // along the ray
        float tp = (e - b * d) / den;       // along the plume
        tp = clamp(tp, 0.0, 50.0);
        vec3 pp = a + uPlumeDir * tp;
        tr = dot(pp, rd);
        if (tr <= 0.0 || tr > scene_t) continue;
        float dist = length(rd * tr - pp);
        float rad = 0.7 + tp * 0.045;
        float k = saturate(1.0 - dist / rad) * exp(-tp / 22.0) * smoothstep(0.0, 1.5, tp);
        s += k;
    }
    if (s <= 0.0) return vec2(0.0);
    vec2 q = uv * vec2(90.0, 60.0);
    vec2 n = vec2(sin(q.y + uTime * 23.0 + sin(q.x * 0.7 + uTime * 11.0)),
                  cos(q.x * 1.3 - uTime * 19.0 + sin(q.y * 0.9)));
    return n * s * uHaze * 0.0025;
}


void main() {
    float dist0 = texture(uDistTex, vUV).r;
    vec2 uv = vUV + exhaust_shimmer(vUV, dist0);
    vec3 scene = texture(uScene, uv).rgb;
    vec2 dd = texture(uDistTex, uv).rg;
    float dist = dd.x;
    float farf = dd.y;          // the share of an edge pixel that is background

    // The clouds were marched past anything near - the aircraft - to what
    // lies behind, so every cloud texel holds the cloud behind it too. Here,
    // per tap, it is laid over this pixel only if that cloud is in front of
    // what the pixel shows: its depth (the cloud's light-weighted distance)
    // against the pixel's. Over the aircraft, cloud behind it is left out;
    // round it, the sky gets its cloud - no halo, at any cloud resolution.
    vec2 lp = uv * uCloudRes - 0.5;
    ivec2 l0 = ivec2(floor(lp));
    vec2 f = lp - vec2(l0);
    ivec2 lm = ivec2(uCloudRes) - 1;
    vec4 cl = vec4(0.0);
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x) {
        ivec2 lc = clamp(l0 + ivec2(x, y), ivec2(0), lm);
        float wb = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
        vec4 ct = texelFetch(uClouds, lc, 0);
        float cd = max(texelFetch(uCloudDepth, lc, 0).r, 1.0);
        // in front of the nearest surface in this pixel, or (for an edge's
        // background share) of whatever is behind it
        // gradual: inside a cloud the aircraft's far end sits part way into
        // it, and a hard switch on a noisy depth speckles
        float rr = dist / cd;
        float vis = 1.0 - exp(-1.6 * rr * rr);
        vis = mix(vis, 1.0, farf);
        cl += mix(vec4(0.0, 0.0, 0.0, 1.0), ct, vis) * wb;
    }
    vec3 col = scene * cl.a + cl.rgb;

    // the passage: inside a bank of cloud, grey-white and moving past
    if (uPassage > 0.0) {
        vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb * 0.8 +
                   texelFetch(uProbe, ivec2(3, 0), 0).rgb * 0.14;
        vec2 c = vUV - 0.5;
        float r = length(c);
        float swirl = 0.5 + 0.5 * sin(r * 30.0 - uTime * 6.0 + atan(c.y, c.x) * 3.0);
        float fog = saturate(uPassage * 1.15) * mix(0.9, 1.0, swirl * uPassage);
        col = mix(col, amb, fog);
    }
    col += vec3(0.7, 0.75, 1.0) * uFlashScreen;
    frag = vec4(col, 1.0);
}
