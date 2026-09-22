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

// Vertical profile of a layer. h: 0 at the base, 1 at the top.
// type 0 stratus, 0.5 cumulus, 1 cumulonimbus.
float height_profile(float h, float type) {
    float stratus = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.5, 1.0, h));
    float cumulus = smoothstep(0.0, 0.1, h) * (1.0 - smoothstep(0.3, 1.0, h));
    float cb      = smoothstep(0.0, 0.05, h) * (1.0 - smoothstep(0.9, 1.0, h));
    float a = saturate(type * 2.0), b = saturate(type * 2.0 - 1.0);
    return mix(mix(stratus, cumulus, a), cb, b);
}

float layer_density(vec3 air3, float alt, vec4 l0, vec4 l1, float cover_map,
                    float height_map, bool detail) {
    float base = l0.x, top = l0.y, type = l0.w;
    // each cloud reaches its own height: cumulus towers do not end flat
    float local_top = base + (top - base) * (type < 0.3 ? 1.0 : mix(0.35, 1.0, height_map));
    if (alt < base || alt > local_top || cover_map < 0.01) return 0.0;
    float h = (alt - base) / max(local_top - base, 1.0);
    float scale = l1.y * 3000.0;

    // anvils: the tops of storms spread out and are blown downwind
    float anvil = smoothstep(0.62, 0.95, h) * saturate(type * 3.0 - 2.0);
    air3.xz += uShearDir * h * h * 4000.0 * saturate(type * 2.0 - 1.0);
    float cover = saturate(cover_map + anvil * 0.45);

    vec4 n = texture(uNoiseBase, air3 / scale);
    float lowfreq = n.g * 0.625 + n.b * 0.25 + n.a * 0.125;
    float shape = saturate(remap(n.r, lowfreq - 1.0, 1.0, 0.0, 1.0));
    // the tops heave: each cell reaches its own height
    shape *= height_profile(saturate(h * (1.0 + (0.55 - n.g) * 0.7)), type);
    // coverage cuts the field where it is thin; once, not twice, or a 40%
    // sky erodes to nothing
    shape = saturate(remap(shape, 1.0 - cover, 1.0, 0.0, 1.0));
    if (shape <= 0.0) return 0.0;
    if (detail) {
        vec3 dn = texture(uNoiseDetail, air3 / (scale * 0.11) + vec3(0.0, uTime * 0.004, 0.0)).rgb;
        float dfbm = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
        // wispy underneath, cauliflower on top
        float mod_ = mix(dfbm, 1.0 - dfbm, saturate(h * 6.0));
        shape = saturate(remap(shape, mod_ * l1.z * 0.9, 1.0, 0.0, 1.0));
    }
    return shape * l1.x * 2.2;
}

// Density at a camera-relative point. `detail` false is the cheap version
// the light march uses.
float cloud_density(vec3 prel, bool detail, out float hfrac) {
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
