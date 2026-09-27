// Cloud density: shared by the cloud march, the cloud-shadow map and the
// probe. Positions are camera-relative metres; the earth curves away under
// them, so a point's altitude is its distance from the earth's centre, not
// its y.

uniform sampler3D uNoiseBase;     // 128^3 Perlin-Worley + Worley fbm
uniform sampler3D uNoiseDetail;   // 32^3 Worley fbm
uniform sampler2D uWeatherMap;    // R low cover, G low height, B mid cover, A rain

uniform vec2  uAirOffset;         // how far the air mass has drifted
uniform vec2  uWMCenter;          // weather map centre, air-mass metres
uniform float uWMSize;            // weather map side, metres
uniform vec4  uLow0, uLow1;       // base, top, cover, type | density, scale km, erosion, -
uniform vec4  uMid0, uMid1;
uniform float uRain;              // rain / snow intensity, 0..1
uniform float uTime;
uniform vec2  uShearDir;          // where the anvils are blown

const float CLOUD_EXT = 0.045;    // extinction per metre at density 1

float altitude_of(vec3 prel) {
    vec3 c = prel + vec3(0.0, EARTH_R + uCamWorld.y, 0.0);
    return length(c) - EARTH_R;
}

vec4 weather_at(vec2 air) {
    vec2 uv = (air - uWMCenter) / uWMSize + 0.5;
    vec4 w = texture(uWeatherMap, uv);
    // past the edge of the map the weather fades to its average, so a
    // horizon at 300 km never shows the square
    vec2 e = abs(uv - 0.5) * 2.0;
    float fade = 1.0 - smoothstep(0.85, 1.0, max(e.x, e.y));
    return w * fade;
}

// Value noise with a quintic fall-off, so its slope is continuous. The
// weather map cannot serve here: it is bilinear, and the kink in its slope at
// every texel boundary shears the cloud field into facets a few hundred
// metres across - the stair-stepping on a tower's flank.
float cn2(vec2 p) {
    vec2 i = floor(p), f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash2i(ivec2(i)), b = hash2i(ivec2(i) + ivec2(1, 0));
    float c = hash2i(ivec2(i) + ivec2(0, 1)), d = hash2i(ivec2(i) + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Vertical profile of a layer. h: 0 at the base, 1 at the top.
// type 0 stratus, 0.5 cumulus, 1 cumulonimbus.
float height_profile(float h, float type) {
    float stratus = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.5, 1.0, h));
    float cumulus = smoothstep(0.0, 0.1, h) * (1.0 - smoothstep(0.3, 1.0, h));
    float cb      = smoothstep(0.0, 0.05, h) * (1.0 - smoothstep(0.9, 1.0, h));
    float a = saturate(type * 2.0), b = saturate(type * 2.0 - 1.0);
    return mix(mix(stratus, cumulus, a), cb, b);
}

// `detail` is how much of the fine erosion to apply, 0 to 1: a hard switch
// between on and off draws a visible ring round the camera where the tone
// changes, so it is faded with distance instead.
float layer_density(vec3 air3, float alt, vec4 l0, vec4 l1, float cover_map,
                    float height_map, float detail) {
    float base = l0.x, top = l0.y, type = l0.w;
    // each cloud reaches its own height: cumulus towers do not end flat
    // Each cloud reaches its own height - a flat layer included: held to one
    // level, a deck's top is a dead flat ceiling and the eye reads it at once.
    float local_top = base + (top - base) * (type < 0.3 ? mix(0.55, 1.05, height_map)
                                                        : mix(0.35, 1.0, height_map));
    if (alt < base || alt > local_top || cover_map < 0.01) return 0.0;
    float h = (alt - base) / max(local_top - base, 1.0);
    float scale = l1.y * 3000.0;
    // The noise the shapes come from is a texture that repeats every `scale`
    // metres, and across a deck tens of kilometres wide that grid of
    // identical clumps is what the eye picks out. Sliding the lookup about
    // breaks the grid.
    //
    // A flat layer gets it everywhere. Heaped cloud does not: dragging a
    // tower sideways bends it against the wind and shows as steps down its
    // flank. It gets the plain lookup, and the drag only in occasional
    // regions - where a mass of air is rising or sinking, and the cloud in it
    // is twisted and sheared.
    // Two drags, and they do different jobs.
    //
    // The slow one, over tens of kilometres, every layer gets. It slides the
    // tile about bodily - whole clouds move together, nothing is sheared, no
    // tower leans - and that alone is enough to kill the repeat in big
    // heaped masses.
    vec2 qs = air3.xz / (scale * 9.0);
    air3.xz += (vec2(cn2(qs), cn2(qs + 3.1)) - 0.5) * scale * 1.7;
    //
    // The quick one makes the swirls. A flat layer gets it everywhere.
    // Heaped cloud would be bent against the wind by it, so it only comes in
    // where a mass of air is rising or sinking, and gently.
    float flatness = 1.0 - smoothstep(0.22, 0.5, type);
    vec2 q = air3.xz / (scale * 2.4);
    float region = cn2(q * 0.42 + 11.3);
    float updraught = smoothstep(0.60, 0.88, region) + smoothstep(0.40, 0.12, region);
    float amt = mix(min(updraught, 1.0) * 0.45, 1.0, flatness);
    if (amt > 0.001) {
        vec2 w2 = vec2(cn2(q), cn2(q + 5.7)) - 0.5;
        air3.xz += w2 * scale * 1.15 * amt;
    }

    // anvils: the tops of storms spread out and are blown downwind
    float anvil = smoothstep(0.62, 0.95, h) * saturate(type * 3.0 - 2.0);
    air3.xz += uShearDir * h * h * 4000.0 * saturate(type * 2.0 - 1.0);
    float cover = saturate(cover_map + anvil * 0.45);

    // The vertical is sampled finer than the horizontal: a shallow layer
    // spans only a fraction of a noise cell, so its top comes out as a smooth
    // height field - gravel seen from above - instead of billowing.
    vec4 n = texture(uNoiseBase, vec3(air3.x, air3.y * 1.7, air3.z) / scale);
    float lowfreq = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float shape = saturate(remap(n.r, lowfreq - 1.0, 1.0, 0.0, 1.0));
    // the tops heave: each cell reaches its own height
    shape *= height_profile(saturate(h * (1.0 + (0.55 - n.g) * 0.7)), type);
    // coverage cuts the field where it is thin; once, not twice, or a 40%
    // sky erodes to nothing
    shape = saturate(remap(shape, 1.0 - cover, 1.0, 0.0, 1.0));
    if (shape <= 0.0) return 0.0;
    if (detail > 0.001) {
        // The detail texture is its own tile, a tenth of the shape's, so it
        // repeats every few hundred metres: close to, that grid is plain to
        // see as a corduroy across the cloud. It gets its own drag, over a
        // few kilometres, which breaks it without smearing anything.
        vec3 dp = air3 / (scale * 0.11) + vec3(0.0, uTime * 0.004, 0.0);
        vec2 dq = air3.xz / (scale * 0.55);
        dp.xz += (vec2(cn2(dq), cn2(dq + 9.3)) - 0.5) * 1.1;
        vec3 dn = texture(uNoiseDetail, dp).rgb;
        float dfbm = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
        // wispy underneath, cauliflower on top
        float mod_ = mix(dfbm, 1.0 - dfbm, saturate(h * 6.0));
        shape = saturate(remap(shape, mod_ * l1.z * 1.2 * detail, 1.0, 0.0, 1.0));
    }
    return shape * l1.x * 2.2;
}

// Density at a camera-relative point. `detail` 0 is the cheap version the
// light march uses; between 0 and 1 the fine erosion fades in.
float cloud_density(vec3 prel, float detail, out float hfrac) {
    float alt = altitude_of(prel);
    vec2 air = uCamWorld.xz + prel.xz - uAirOffset;
    vec4 w = weather_at(air);
    vec3 air3 = vec3(air.x, alt, air.y);
    float d = layer_density(air3, alt, uLow0, uLow1, w.r, w.g, detail);
    if (uMid0.z > 0.0)
        d += layer_density(air3 + vec3(5000.0, 0.0, 3000.0), alt, uMid0, uMid1, w.b, 0.8, detail);
    hfrac = saturate((alt - uLow0.x) / max(uLow0.y - uLow0.x, 1.0));
    return d;
}

// Rain (or snow) falling out of the base of the low layer: thin, grey,
// visible as curtains against the light.
float rain_density(vec3 prel) {
    if (uRain <= 0.0) return 0.0;
    float alt = altitude_of(prel);
    if (alt > uLow0.x + 200.0) return 0.0;
    vec2 air = uCamWorld.xz + prel.xz - uAirOffset;
    // rain falls and the wind leans the shafts
    float lean = (uLow0.x - alt) * 0.25;
    vec4 w = weather_at(air + uShearDir * lean);
    float shaft = w.a * smoothstep(uLow0.x + 200.0, uLow0.x - 100.0, alt);
    float streak = texture(uNoiseBase, vec3(air.x / 2500.0, alt / 9000.0, air.y / 2500.0)).g;
    return shaft * mix(0.5, 1.2, streak) * 0.004;
}
