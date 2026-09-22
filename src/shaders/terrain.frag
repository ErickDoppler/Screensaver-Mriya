// The ground, shaded per pixel from the same height function the vertices
// used - with as much detail as the pixel can show - so that a level's coarse
// triangles never show and the seam between two levels has nothing to differ
// by. The look is a satellite photograph's: the colour is in the land cover,
// the relief is in the light.
in vec3 vRel;
in vec2 vWorld;
in float vH;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragDist;

uniform vec4  uHole;          // finer level's footprint, camera-relative xz (min x, min z, max x, max z)
uniform float uPixelAngle;    // radians per pixel
uniform vec2  uRes;
uniform vec3  uLightDir;
uniform vec3  uLightIllum;
uniform vec3  uMoonDir2;      // the other light, dimmer
uniform vec3  uMoonIllum2;
uniform sampler2D uProbe;
uniform sampler2D uSkyView;
uniform sampler2D uCloudShadow;
uniform vec2  uCSCenter;
uniform float uCSSize;
uniform float uNight;         // 0 day .. 1 full night: town lights on
uniform float uCityLights;
uniform float uWetness;
uniform float uSeaState;
uniform float uTime;
uniform float uFlash;         // lightning lights the ground too
uniform vec3  uFlashRel;
uniform sampler2D uAcShadow;  // the aircraft's shadow map
uniform mat4  uAcShadowMat;   // camera-relative -> shadow map clip space
uniform float uAcShadowOn;

float cam_rkm() { return (EARTH_R + uCamWorld.y) * 0.001; }

/// --- land cover --------------------------------------------------------------
// Albedos are linear reflectances, as a satellite would measure them: green
// crops and forest are dark (0.05-0.1), bare soil and stubble brighter, sand
// and snow brightest. Anything finer than a pixel fades to its average.
float n2(vec2 p, uint s) { return tnoise(p, s) * 0.5 + 0.5; }
float f2(vec2 p, int o, uint s) { return tfbm(p, o, s) * 0.5 + 0.5; }
// fbm whose octaves finer than the pixel are dropped
float f2l(vec2 w, float scale, int o, uint s, float fp) {
    return tfbm_lim(w / scale, scale, o, s, fp * 2.0) * 0.5 + 0.5;
}
// Coverage of a line of half-width hw metres at distance d. A line thinner
// than the pixel fades to the share of the pixel it covers, instead of
// flickering in and out.
float line_mix(float d, float hw, float fp) {
    float soft = max(fp, 0.5);
    float c = 1.0 - smoothstep(hw - soft * 0.5, hw + soft * 0.5, d);
    return fp > hw * 2.0 ? c * (hw * 2.0 / fp) : c;
}

vec3 crop_colour(float h) {
    if (h < 0.22) return vec3(0.29, 0.24, 0.12);    // ripe wheat
    if (h < 0.40) return vec3(0.085, 0.13, 0.045);  // green crop
    if (h < 0.55) return vec3(0.065, 0.052, 0.038); // ploughed black earth
    if (h < 0.67) return vec3(0.24, 0.20, 0.12);    // stubble
    if (h < 0.75) return vec3(0.12, 0.13, 0.05);    // sunflower
    if (h < 0.79) return vec3(0.42, 0.38, 0.06);    // rapeseed in flower
    if (h < 0.90) return vec3(0.10, 0.13, 0.055);   // pasture
    return vec3(0.15, 0.15, 0.085);                 // fallow
}

// The patchwork of the steppe: large blocks each turned its own way, cut into
// strips, divided by shelter belts and dirt roads.
vec3 fields(vec2 w, float fp, vec4 T4, out float belt, out float road) {
    uint seed = uint(uT3.w);
    float size = T4.x * 1000.0;
    // Districts a dozen kilometres across share an orientation and a scale.
    // Not squares: cells round scattered centres, their edges bent by a
    // warp, so a boundary wanders the way a parish line does - a straight
    // 12 km grid showed as a long seam wherever the track ran along it.
    vec2 wr = w + vec2(tnoise(w / 9000.0, seed + 71u), tnoise(w / 9000.0 + 3.7, seed + 72u)) * 2600.0;
    vec2 g0 = floor(wr / 12000.0);
    vec2 rc = g0;
    float d1 = 1e9, d2 = 1e9;
    for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
        vec2 cc = g0 + vec2(i, j);
        vec2 pc = (cc + 0.15 + 0.7 * vec2(hash2i(ivec2(cc) + ivec2(seed, 61)), hash2i(ivec2(cc) + ivec2(62, seed)))) * 12000.0;
        float d = length(wr - pc);
        if (d < d1) { d2 = d1; d1 = d; rc = cc; }
        else if (d < d2) d2 = d;
    }
    float redge = (d2 - d1) * 0.5;               // metres to the district's edge
    float rh = hash2i(ivec2(rc) + ivec2(seed, 17));
    float ang = (rh - 0.5) * 1.2;
    vec2 cs = vec2(cos(ang), sin(ang));
    // a slight wobble, so boundaries follow the lie of the land
    vec2 wob = vec2(tnoise(w / 2200.0, seed + 5u), tnoise(w / 2200.0 + 9.1, seed + 6u)) * 35.0;
    vec2 ww = w + wob;
    vec2 r = vec2(dot(ww, cs), dot(ww, vec2(-cs.y, cs.x)));
    float bsz = size * mix(0.7, 1.5, hash2i(ivec2(rc) + ivec2(3, seed)));
    vec2 bsize = vec2(bsz, bsz * mix(0.6, 1.1, hash2i(ivec2(rc) + ivec2(8, seed))));
    vec2 bc = floor(r / bsize);
    vec2 bf = r / bsize - bc;
    float bh = hash2i(ivec2(bc) + ivec2(seed, 31));
    // strips inside the block, along one axis or the other
    int nstrip = 1 + int(hash2i(ivec2(bc) + ivec2(seed, 41)) * 4.0);
    bool along_x = bh < 0.5;
    float sc = (along_x ? bf.x : bf.y) * float(nstrip);
    float strip = floor(sc);
    float sf = fract(sc);
    float fh = hash2i(ivec2(bc) * 7 + ivec2(int(strip), 3) + ivec2(seed));
    vec3 c = crop_colour(fh);
    // regional tint: some districts greener, some drier
    c *= mix(vec3(0.85, 0.95, 0.85), vec3(1.1, 1.0, 0.9), f2(w / 40000.0, 3, seed + 7u));
    // moisture and soil within a field
    c *= 0.8 + 0.4 * f2l(w, 450.0, 4, seed + 9u, fp);
    // tractor lines along the field
    float lines = 0.5 + 0.5 * sin((along_x ? r.y : r.x) * 0.55 + fh * 40.0);
    c *= mix(1.0, 0.92 + 0.12 * lines, saturate(2.0 - fp * 0.6));
    // edges, in metres
    vec2 be = min(bf, 1.0 - bf) * bsize;
    float bedge = min(be.x, be.y);
    float sedge = min(sf, 1.0 - sf) * (along_x ? bsize.x : bsize.y) / float(nstrip);
    belt = line_mix(bedge, 7.0, fp) * step(0.3, hash2i(ivec2(bc) + ivec2(11, seed)));
    road = max(line_mix(sedge, 2.5, fp) * 0.6,
               line_mix(abs(bedge - 11.0), 2.5, fp) * step(hash2i(ivec2(bc) + ivec2(19, seed)), 0.5));
    // along the district's edge a road, lined with a belt of trees
    road = max(road, line_mix(redge, 4.0, fp));
    belt = max(belt, line_mix(abs(redge - 12.0), 6.0, fp) * 0.85);
    return c;
}

// Towns: most cells of a coarse grid hold none; those that do have a street
// grid of their own, roofs, gardens, and at night lights along the streets.
float town(vec2 w, float fp, vec4 T4, out vec3 col, out float lights) {
    uint seed = uint(uT3.w);
    lights = 0.0;
    col = vec3(0.0);
    float cellsz = 5000.0;
    vec2 cc = floor(w / cellsz);
    float best = 0.0;
    vec2 bctr = vec2(0.0);
    float bang = 0.0, brad = 1.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec2 c = cc + vec2(x, y);
        float h = hash2i(ivec2(c) + ivec2(seed, 91));
        if (h > T4.z * 0.6) continue;
        vec2 ctr = (c + 0.2 + 0.6 * vec2(hash2i(ivec2(c) + ivec2(3, 5)), hash2i(ivec2(c) + ivec2(9, 1)))) * cellsz;
        float rad = 250.0 + 1700.0 * pow(hash2i(ivec2(c) + ivec2(17, 23)), 3.0);
        float d = length(w - ctr) / rad;
        d += (f2(w / 350.0, 3, seed + 71u) - 0.5) * 0.7;
        float v = 1.0 - smoothstep(0.55, 1.0, d);
        if (v > best) { best = v; bctr = ctr; bang = hash2i(ivec2(c) + ivec2(29, 7)) * 1.57; brad = rad; }
    }
    if (best <= 0.0) return 0.0;
    vec2 cs = vec2(cos(bang), sin(bang));
    vec2 q = w - bctr;
    vec2 r = vec2(dot(q, cs), dot(q, vec2(-cs.y, cs.x)));
    float block = mix(70.0, 110.0, hash2i(ivec2(bctr)));
    vec2 bf = fract(r / block);
    vec2 be = min(bf, 1.0 - bf) * block;
    float street = line_mix(min(be.x, be.y), 4.5, fp);
    // lots inside the blocks: roofs of different colours, and gardens
    vec2 lot = floor(r / (block * 0.25));
    float lh = hash2i(ivec2(lot) + ivec2(seed, 5));
    vec3 roof = lh < 0.35 ? vec3(0.16, 0.07, 0.05) : (lh < 0.7 ? vec3(0.18, 0.18, 0.18) : vec3(0.28, 0.27, 0.25));
    float centre = 1.0 - smoothstep(0.0, 1.0, length(q) / brad);
    float garden = step(mix(0.75, 0.2, centre), hash2i(ivec2(lot) + ivec2(13, seed)));
    vec3 lotc = mix(roof, vec3(0.06, 0.09, 0.035), garden);
    // too small to see individual roofs: the blend of them
    vec3 avg = mix(vec3(0.16, 0.15, 0.13), vec3(0.09, 0.1, 0.06), mix(0.55, 0.2, centre));
    lotc = mix(lotc, avg, saturate((fp - 1.5) / 3.0));
    col = mix(lotc, vec3(0.14, 0.13, 0.12), street);
    // lamps along the streets; from afar, the glow of the whole grid
    float lamps = line_mix(min(be.x, be.y), 2.0, fp) * (0.6 + 0.4 * hash2i(ivec2(floor(r / 30.0))));
    // From afar not a lit disc but a web: the arterial roads every few
    // blocks, and neighbourhoods patchy with darker parks and yards between.
    vec2 af = fract(r / (block * 5.0));
    vec2 ae = min(af, 1.0 - af) * block * 5.0;
    float arterial = line_mix(min(ae.x, ae.y), 9.0, fp);
    float patch = smoothstep(0.35, 0.8, f2(w / 420.0, 3, seed + 133u));
    float far = (0.06 + 0.3 * centre * centre) * patch + arterial * (0.35 + 0.4 * centre);
    lights = best * mix(lamps, far, saturate(fp / 25.0)) * (0.6 + centre);
    return best;
}

vec3 land_albedo_b(vec2 w, float h, vec3 n, float fp, int biome, vec4 T3, vec4 T4, out float snow_amt, out float emissive) {
    uint seed = uint(uT3.w);
    float slope = 1.0 - n.y;
    emissive = 0.0;
    vec3 c;
    float forest_mask = smoothstep(0.52, 0.6, f2(w / 3500.0, 4, seed + 101u) + (T4.y - 0.5) * 0.6);
    // canopy: soft clumps, never pixels
    vec3 forest = vec3(0.035, 0.06, 0.025) * (0.65 + 0.7 * f2l(w, 90.0, 4, seed + 3u, fp));
    if (biome == 3) {
        // desert: sand, darker gravel plains and bare rock ranges
        vec3 sand = mix(vec3(0.42, 0.3, 0.17), vec3(0.52, 0.38, 0.22), f2l(w, 1500.0, 4, seed + 7u, fp));
        vec3 gravel = vec3(0.22, 0.16, 0.1);
        c = mix(sand, gravel, smoothstep(0.45, 0.7, f2(w / 7000.0, 3, seed + 9u)));
        c = mix(c, vec3(0.2, 0.12, 0.07), smoothstep(0.15, 0.4, slope));
        forest_mask = 0.0;
    } else if (biome == 4) {
        c = vec3(0.78, 0.82, 0.86);
        c = mix(c, vec3(0.08, 0.075, 0.07), smoothstep(0.35, 0.6, slope));
        forest_mask = 0.0;
    } else if (biome == 2 || biome == 8) {
        // alpine: meadows and forest below, scree and rock above. In the
        // Himalaya the meadows are the dry ochre grass of the high plateau
        vec3 meadow = biome == 8
            ? mix(vec3(0.2, 0.15, 0.08), vec3(0.14, 0.13, 0.07), f2l(w, 900.0, 4, seed + 13u, fp))
            : vec3(0.08, 0.11, 0.04) * (0.8 + 0.4 * f2l(w, 600.0, 4, seed + 13u, fp));
        vec3 rock = mix(vec3(0.1, 0.095, 0.09), vec3(0.2, 0.18, 0.16), f2l(w, 300.0, 5, seed + 17u, fp));
        if (biome == 8) rock *= vec3(1.05, 0.9, 0.75);   // the warm brown rock of the high Himalaya
        float tl = T3.y + (n2(w / 2000.0, seed + 19u) - 0.5) * 400.0;
        c = mix(meadow, rock, saturate(smoothstep(tl - 200.0, tl + 300.0, h) + smoothstep(0.3, 0.55, slope)));
        forest_mask *= 1.0 - smoothstep(tl - 300.0, tl, h);
        forest_mask = max(forest_mask, (1.0 - smoothstep(tl - 400.0, tl - 100.0, h)) * smoothstep(0.1, 0.3, slope) * 0.8);
    } else if (biome == 5) {
        c = vec3(0.04, 0.09, 0.025) * (0.75 + 0.5 * f2l(w, 500.0, 4, seed + 23u, fp));
        forest = vec3(0.02, 0.055, 0.015) * (0.7 + 0.6 * f2l(w, 60.0, 4, seed + 3u, fp));
    } else {
        float belt, road;
        c = fields(w, fp, T4, belt, road);
        c = mix(c, forest * 1.1, belt * 0.9);
        c = mix(c, vec3(0.2, 0.18, 0.14), road);
        if (biome == 6) c = mix(c, vec3(0.1, 0.11, 0.06), 0.35);
        c = mix(c, vec3(0.13, 0.12, 0.09), smoothstep(0.25, 0.5, slope));
    }
    c = mix(c, forest, forest_mask);
    // shores: a strip of sand or mud where land meets water, and the green of
    // the river banks
    float shore = 1.0 - smoothstep(0.5, biome == 5 ? 9.0 : 3.5, h);
    vec3 bank = biome == 5 || biome == 1 ? vec3(0.5, 0.45, 0.32) : vec3(0.06, 0.08, 0.035);
    c = mix(c, bank, shore * 0.85);
    // towns
    if (T4.z > 0.0) {
        vec3 tc;
        float lights;
        float built = town(w, fp, T4, tc, lights) * (1.0 - shore);
        c = mix(c, tc, built * 0.9);
        emissive = lights * (1.0 - shore);
    }
    // snow: the snow line, the season, flat ground holding more than steep
    float sline = T3.x + (n2(w / 1500.0, seed + 29u) - 0.5) * 500.0;
    snow_amt = smoothstep(sline - 150.0, sline + 150.0, h);
    snow_amt = max(snow_amt, T4.w);
    snow_amt *= 1.0 - smoothstep(0.45, 0.75, slope);
    snow_amt *= 1.0 - forest_mask * 0.6;
    c = mix(c, vec3(0.8, 0.83, 0.87), snow_amt);
    // wet ground is darker
    c *= 1.0 - uWetness * 0.25 * (1.0 - snow_amt);
    return c;
}

// While the scenery changes the land cover cross-fades from one biome's to
// the next one's.
int biome_now() { return int((uT5.y > 0.5 ? uT5.x : uT3.z) + 0.5); }
vec3 land_albedo(vec2 w, float h, vec3 n, float fp, out float snow_amt, out float emissive) {
    int b1 = int(uT3.z + 0.5), b2 = int(uT5.x + 0.5);
    float m = uT5.y;
    if (m <= 0.001) return land_albedo_b(w, h, n, fp, b1, uT3, uT4, snow_amt, emissive);
    if (m >= 0.999) return land_albedo_b(w, h, n, fp, b2, uU3, uU4, snow_amt, emissive);
    float s1, e1, s2, e2;
    vec3 c1 = land_albedo_b(w, h, n, fp, b1, uT3, uT4, s1, e1);
    vec3 c2 = land_albedo_b(w, h, n, fp, b2, uU3, uU4, s2, e2);
    snow_amt = mix(s1, s2, m);
    emissive = mix(e1, e2, m);
    return mix(c1, c2, m);
}

float aircraft_shadow(vec3 rel) {
    if (uAcShadowOn <= 0.0) return 1.0;
    vec4 c = uAcShadowMat * vec4(rel, 1.0);
    vec3 s = c.xyz / c.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    // the sun is half a degree wide: the further behind the aircraft, the
    // softer the shadow, until it is only a darker smudge
    float occ = 0.0;
    float spread = 0.012 + 0.0;
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 2.39996 + ign(gl_FragCoord.xy, uTime * 60.0) * 6.28;
        float r = sqrt((float(i) + 0.5) / 8.0) * spread;
        vec2 o = vec2(cos(a), sin(a)) * r;
        float d = texture(uAcShadow, s.xy + o).r;
        occ += step(d, s.z - 0.002) * step(d, 0.9999);
    }
    return 1.0 - occ / 8.0 * 0.85 * uAcShadowOn;
}

void main() {
    if (vRel.x > uHole.x && vRel.x < uHole.z && vRel.z > uHole.y && vRel.z < uHole.w) discard;
    float dist = length(vRel);
    float fp = max(dist * uPixelAngle, 0.25);            // metres per pixel here
    float minw = fp * 2.0;
    vec2 w = vWorld;

    // height and normal at this pixel, with the detail it can hold
    float e = max(fp, 0.5);
    float h = terrain_height(w, minw);
    float hx = terrain_height(w + vec2(e, 0.0), minw);
    float hz = terrain_height(w + vec2(0.0, e), minw);
    vec3 n = normalize(vec3(h - hx, e, h - hz));
    vec3 V = -vRel / dist;
    vec2 suv = gl_FragCoord.xy / uRes;

    float rkm = ATM_BOTTOM + max(h, 0.0) * 0.001;
    vec3 sun = uLightIllum * transmittance(rkm, uLightDir.y);
    vec3 moon = uMoonIllum2 * transmittance(rkm, uMoonDir2.y);
    vec3 sky_amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb;

    // cloud shadow: look up where this point's sun ray reaches sea level
    vec2 ref = w - uLightDir.xz / max(uLightDir.y, 0.06) * max(h, 0.0);
    float cs = texture(uCloudShadow, (ref - uCSCenter) / uCSSize + 0.5).r;
    float shadow = cs * aircraft_shadow(vRel);

    vec3 col;
    if (h < 0.4) {
        // --- water ---------------------------------------------------------
        float depth = -h;
        vec2 wp = w * 0.08;
        float t = uTime;
        float amp = mix(0.15, 1.0, uSeaState);
        vec3 wn = vec3(0.0);
        // a few octaves of wind waves, finest ones fading with distance
        for (int i = 0; i < 4; ++i) {
            float sc = exp2(float(i));
            float fade = saturate(3.0 - fp * sc * 0.08);
            vec3 nd = tnoised(wp * sc + vec2(t * 0.35 * sc, t * 0.2), 900u + uint(i));
            wn.xz += nd.yz * amp * fade * 0.12 / sc;
        }
        vec3 N = normalize(vec3(-wn.x, 1.0, -wn.z));
        float ndv = saturate(dot(N, V));
        float F = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
        vec3 R = reflect(-V, N);
        R.y = abs(R.y);
        vec3 refl = textureLod(uSkyView, skyview_uv(R, cam_rkm()), 0.0).rgb;
        int biome = biome_now();
        vec3 deep = biome == 4 ? vec3(0.01, 0.025, 0.035) : vec3(0.004, 0.02, 0.035);
        vec3 shallow = biome == 5 ? vec3(0.05, 0.3, 0.3) : vec3(0.03, 0.09, 0.08);
        vec3 body = mix(shallow, deep, smoothstep(1.0, biome == 5 ? 25.0 : 8.0, depth));
        vec3 amb = sky_amb * body * PI * 0.35;
        col = body * sun * saturate(uLightDir.y) * shadow * 0.35 + amb;
        col = mix(col, refl, F);
        // the sun's glitter: a broad glint from the wave slopes
        float rough = mix(0.04, 0.22, uSeaState) + fp * 0.0004;
        vec3 H = normalize(uLightDir + V);
        float nh = saturate(dot(N, H));
        float a2 = rough * rough;
        float D = a2 / (PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
        col += sun * shadow * D * F * 0.25 * saturate(uLightDir.y * 4.0);
        vec3 Hm = normalize(uMoonDir2 + V);
        float nhm = saturate(dot(N, Hm));
        col += moon * a2 / (PI * pow(nhm * nhm * (a2 - 1.0) + 1.0, 2.0)) * F * 0.25;
        // sea ice in the arctic: floes with dark leads between
        if (biome == 4) {
            float floe = tnoise(w / 900.0, 77u) * 0.5 + tnoise(w / 230.0, 78u) * 0.3;
            float ice = smoothstep(-0.05, 0.05, floe) * smoothstep(-6.0, 0.4, h + 3.0);
            vec3 icec = vec3(0.85, 0.9, 0.95) * (sun * saturate(uLightDir.y) * shadow / PI + sky_amb);
            col = mix(col, icec, ice);
        }
    } else {
        // --- land ------------------------------------------------------------
        float snow_amt, em;
        vec3 alb = land_albedo(w, h, n, fp, snow_amt, em);
        float ndl = saturate(dot(n, uLightDir));
        float ndm = saturate(dot(n, uMoonDir2));
        vec3 diff = alb / PI * (sun * ndl * shadow + moon * ndm);
        vec3 amb = alb * sky_amb * (0.6 + 0.4 * n.y);
        col = diff + amb;
        // a little sheen on snow and wet ground toward the sun
        if (snow_amt > 0.0 || uWetness > 0.0) {
            vec3 H = normalize(uLightDir + V);
            float spec = pow(saturate(dot(n, H)), 60.0) * (snow_amt * 0.15 + uWetness * 0.2);
            col += sun * shadow * spec;
        }
        // the towns at night
        col += vec3(1.0, 0.68, 0.36) * em * uNight * uCityLights * 0.02;
    }
    // lightning lights the ground under the storm
    if (uFlash > 0.0) {
        vec3 fd = vRel - uFlashRel;
        col += vec3(0.7, 0.75, 1.0) * uFlash * 3e6 / (dot(fd, fd) + 4e6) * 0.08;
    }
    vec3 S, T;
    aerial(suv, dist, S, T);
    col = col * T + S;
    fragColor = vec4(col, 1.0);
    fragDist = vec4(dist, 0.0, 0.0, 1.0);
}
