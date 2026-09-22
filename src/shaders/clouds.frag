// The clouds: one ray per pixel (at reduced resolution) marched through the
// cloud layers, lit by the sun or the moon with a short march toward it, by
// the sky from above and the ground from below, by lightning inside them and
// by the aircraft's own strobes when it is flying through them.
//
// Output: rgb = light scattered toward the camera (aerial perspective
// applied), a = how much of what is behind still shows through.
in vec2 vUV;
layout(location = 0) out vec4 frag;
layout(location = 1) out vec4 fragDepth;

uniform mat3  uCamBasis;
uniform vec2  uTanHalf;
uniform sampler2D uDistTex;     // scene distance, metres (sky: huge)
uniform sampler2D uProbe;
uniform vec2  uFullRes;         // size of the distance texture
uniform float uFrame;
uniform int   uSteps;
uniform float uMaxDist;
uniform float uHMin, uHMax;     // the slab everything lives in
uniform vec3  uLightDir;
uniform vec3  uLightIllum;      // outside the atmosphere
uniform vec2  uCirrus;          // cover, altitude
uniform vec2  uCirrusDir;
uniform float uFlash;
uniform vec3  uFlashRel;
uniform vec4  uPointPos[4];     // camera-relative, w = on (0/1)
uniform vec3  uPointCol[4];
uniform vec3  uCityGlow;        // the lit towns' glow on the undersides
uniform float uGlory;           // how much of the pilot's glory to show

float cam_r_km() { return (EARTH_R + uCamWorld.y) * 0.001; }

vec3 light_at(vec3 prel) {
    vec3 c = prel + vec3(0.0, EARTH_R + uCamWorld.y, 0.0);
    float r = length(c);
    return uLightIllum * transmittance(r * 0.001, dot(c / r, uLightDir));
}

// The slab [uHMin, uHMax] along the ray, respecting the earth's curve.
vec2 slab(vec3 rd) {
    vec3 ro = vec3(0.0, EARTH_R + uCamWorld.y, 0.0);
    float rmin = EARTH_R + uHMin, rmax = EARTH_R + uHMax;
    vec2 outer = ray_sphere(ro, rd, rmax);
    vec2 inner = ray_sphere(ro, rd, rmin);
    float alt = uCamWorld.y;
    float t0, t1;
    if (alt < uHMin) {
        if (inner.y < 0.0) return vec2(1.0, -1.0);
        t0 = inner.y;
        t1 = outer.y;
    } else if (alt > uHMax) {
        if (outer.y < 0.0 || outer.x < 0.0) return vec2(1.0, -1.0);
        t0 = outer.x;
        t1 = inner.x > 0.0 ? inner.x : outer.y;
    } else {
        t0 = 0.0;
        t1 = inner.x > 0.0 ? inner.x : outer.y;
    }
    return vec2(t0, t1);
}

float light_march(vec3 p, vec3 L) {
    float od = 0.0, t = 0.0, dt = 40.0, hf;
    for (int i = 0; i < 6; ++i) {
        od += cloud_density(p + L * (t + dt * 0.5), false, hf) * dt;
        t += dt;
        dt *= 1.9;
    }
    return od * CLOUD_EXT;
}

// Cirrus: a thin, fibrous shell high up, combed out along the wind.
vec4 cirrus(vec3 rd, float scene_t) {
    if (uCirrus.x <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 ro = vec3(0.0, EARTH_R + uCamWorld.y, 0.0);
    vec2 hit = ray_sphere(ro, rd, EARTH_R + uCirrus.y);
    float t = uCamWorld.y < uCirrus.y ? hit.y : hit.x;
    if (t <= 0.0 || t > scene_t || t > 350000.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 p = rd * t;
    vec2 air = uCamWorld.xz + p.xz - uAirOffset;
    vec2 a = vec2(dot(air, uCirrusDir), dot(air, vec2(-uCirrusDir.y, uCirrusDir.x)));
    float n = texture(uNoiseBase, vec3(a.x / 60000.0, 0.37, a.y / 9000.0)).g;
    n = n * 0.7 + texture(uNoiseBase, vec3(a.x / 17000.0, 0.61, a.y / 2600.0)).b * 0.3;
    float d = saturate(remap(n, 1.0 - uCirrus.x * 0.8, 1.0, 0.0, 1.0));
    d *= smoothstep(350000.0, 120000.0, t);
    float alpha = d * 0.55;
    float c = dot(rd, uLightDir);
    vec3 sun = light_at(p) * (hg(c, 0.65) * 0.7 + hg(c, -0.2) * 0.3) * 3.0;
    vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb * 1.2;
    vec3 col = (sun + amb) * alpha;
    vec3 S, T;
    aerial(vUV, t, S, T);
    return vec4(col * T + S * alpha, 1.0 - alpha);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 rd = normalize(uCamBasis * vec3(ndc.x * uTanHalf.x, ndc.y * uTanHalf.y, -1.0));

    // nearest surface under this low-res pixel (conservative: the closest of
    // the full-res pixels it covers)
    vec2 fp = vUV * uFullRes;
    ivec2 ip = ivec2(fp);
    float scene_t = texelFetch(uDistTex, ip, 0).r;
    scene_t = min(scene_t, texelFetch(uDistTex, min(ip + ivec2(1, 0), ivec2(uFullRes) - 1), 0).r);
    scene_t = min(scene_t, texelFetch(uDistTex, min(ip + ivec2(0, 1), ivec2(uFullRes) - 1), 0).r);
    scene_t = min(scene_t, texelFetch(uDistTex, min(ip + ivec2(1, 1), ivec2(uFullRes) - 1), 0).r);

    vec4 ci = cirrus(rd, scene_t);
    bool cirrus_behind = uCamWorld.y < uCirrus.y;

    vec2 tr = slab(rd);
    float t0 = max(tr.x, 0.0), t1 = min(min(tr.y, scene_t), uMaxDist);
    vec3 S = vec3(0.0);
    float T = 1.0;
    float tw = 0.0, ww = 0.0;

    if (t1 > t0) {
        // Ambient inside a cloud is not the blue of the sky: most of the
        // light arriving from all round has been scattered many times by
        // the cloud itself, and it was sunlight to begin with.
        vec3 key_cam = texelFetch(uProbe, ivec2(3, 0), 0).rgb;
        vec3 sky_up = texelFetch(uProbe, ivec2(1, 0), 0).rgb;
        vec3 amb_top = sky_up * 0.55 + key_cam * 0.09;
        vec3 amb_bot = texelFetch(uProbe, ivec2(2, 0), 0).rgb * 0.55 + key_cam * 0.025 + uCityGlow;
        float cos_t = dot(rd, uLightDir);
        // dual-lobe phase: a strong forward peak (the silver lining) and a
        // little back-scatter
        float ph0 = mix(hg(cos_t, 0.82), hg(cos_t, -0.25), 0.25);

        const float C = 400.0;
        float k = pow((t1 + C) / (t0 + C), 1.0 / float(uSteps)) - 1.0;
        float jitter = ign(gl_FragCoord.xy, uFrame);
        float t = t0 + (t0 + C) * k * jitter;
        for (int i = 0; i < 256; ++i) {
            if (i >= uSteps || t >= t1 || T < 0.01) break;
            float dt = (t + C) * k;
            vec3 p = rd * (t + dt * 0.5);
            float hf;
            // the fine detail only where a pixel can hold it
            float d = cloud_density(p, t < 25000.0, hf);
            float rd_ = rain_density(p);
            if (d > 0.0 || rd_ > 0.0) {
                float sigma = d * CLOUD_EXT + rd_;
                vec3 sunL = light_at(p);
                vec3 Ls = vec3(0.0);
                if (d > 0.0) {
                    float od = light_march(p, uLightDir);
                    // multiple scattering as a few octaves of fainter, wider,
                    // less absorbed single scattering (Wrenninge)
                    float a = 1.0, b = 1.0, c = 1.0, ms = 0.0;
                    for (int o = 0; o < 4; ++o) {
                        ms += b * mix(hg(cos_t * c, 0.82 * c), hg(cos_t * c, -0.25 * c), 0.25) * exp(-od * a);
                        a *= 0.25; b *= 0.7; c *= 0.5;
                    }
                    ms *= 1.6;
                    // powder: the dark edges of a sunlit heap
                    float powder = 1.0 - exp(-d * CLOUD_EXT * 120.0);
                    powder = mix(1.0, powder, saturate(0.5 - 0.5 * cos_t) * 0.8);
                    Ls = sunL * ms * powder;
                    Ls += mix(amb_bot, amb_top, saturate(hf * 1.2)) * (0.45 + 0.55 * hf);
                } else {
                    // rain: grey, lit mostly by the sky
                    Ls = sunL * hg(cos_t, 0.4) * 0.15 + mix(amb_bot, amb_top, 0.5) * 0.5;
                }
                // lightning glows through the cloud around it
                if (uFlash > 0.0) {
                    float fd = length(p - uFlashRel);
                    Ls += vec3(0.75, 0.8, 1.0) * uFlash * 60.0 * exp(-fd / 1400.0);
                }
                // the aircraft's own lights, when it is inside the cloud
                for (int l = 0; l < 4; ++l) {
                    if (uPointPos[l].w <= 0.0) continue;
                    vec3 dl = p - uPointPos[l].xyz;
                    Ls += uPointCol[l] / (dot(dl, dl) + 30.0) * (1.0 / (4.0 * PI));
                }
                float st = exp(-sigma * dt);
                vec3 integ = Ls * (1.0 - st);          // (Ls*sigma) * (1-st)/sigma
                S += T * integ;
                float w = T * (1.0 - st);
                tw += w * t;
                ww += w;
                T *= st;
            }
            t += dt;
        }
    }
    // aerial perspective between the camera and the cloud
    if (ww > 0.0) {
        vec3 aS, aT;
        aerial(vUV, tw / ww, aS, aT);
        S = S * aT + aS * (1.0 - T);
    }
    // cirrus sits behind the clouds when we are below it, in front above it
    if (cirrus_behind) {
        S += T * ci.rgb;
        T *= ci.a;
    } else {
        S = ci.rgb + ci.a * S;
        T *= ci.a;
    }
    frag = vec4(S, T);
    // where the cloud is along the ray, for the temporal pass to reproject by
    fragDepth = vec4(ww > 0.0 ? tw / ww : min(scene_t, 1e6), 0.0, 0.0, 1.0);
}
