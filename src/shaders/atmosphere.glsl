// Physically based sky, after Hillaire, "A Scalable and Production Ready Sky
// and Atmosphere Rendering Technique" (EGSR 2020): a transmittance LUT, a
// multiple-scattering LUT, a per-frame sky-view LUT and an aerial-perspective
// volume. Distances in here are kilometres; the rest of the program is metres.

const float ATM_BOTTOM = 6360.0;
const float ATM_TOP = 6460.0;

uniform vec3  uRayleighScat;    // per km at sea level
uniform float uRayleighH;       // km
uniform float uMieScat;         // per km at sea level
uniform float uMieExt;
uniform float uMieH;            // km
uniform float uMieG;
uniform vec3  uMieAbsorb;       // extra, coloured absorption: dust
uniform vec3  uOzoneAbs;        // per km at the peak of the layer

uniform sampler2D uTransLUT;
uniform sampler2D uMultiLUT;

struct Medium { vec3 scat_r; float scat_m; vec3 ext; };

Medium medium_at(float h_km) {
    Medium m;
    h_km = max(h_km, 0.0);
    float dr = exp(-h_km / uRayleighH);
    float dm = exp(-h_km / uMieH);
    float oz = max(0.0, 1.0 - abs(h_km - 25.0) / 15.0);
    m.scat_r = uRayleighScat * dr;
    m.scat_m = uMieScat * dm;
    m.ext = m.scat_r + vec3(uMieExt * dm) + uMieAbsorb * dm + uOzoneAbs * oz;
    return m;
}

float phase_rayleigh(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
// Cornette-Shanks: a Henyey-Greenstein that behaves at back angles.
float phase_mie(float c, float g) {
    float g2 = g * g;
    float k = 3.0 / (8.0 * PI) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5);
}

// --- the transmittance LUT's parameterisation (Bruneton) -------------------
vec2 trans_uv(float r, float mu) {
    float H = sqrt(ATM_TOP * ATM_TOP - ATM_BOTTOM * ATM_BOTTOM);
    float rho = sqrt(max(r * r - ATM_BOTTOM * ATM_BOTTOM, 0.0));
    float disc = r * r * (mu * mu - 1.0) + ATM_TOP * ATM_TOP;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dmin = ATM_TOP - r, dmax = rho + H;
    return vec2((d - dmin) / max(dmax - dmin, 1e-4), rho / H);
}
void trans_params(vec2 uv, out float r, out float mu) {
    float H = sqrt(ATM_TOP * ATM_TOP - ATM_BOTTOM * ATM_BOTTOM);
    float rho = H * uv.y;
    r = sqrt(rho * rho + ATM_BOTTOM * ATM_BOTTOM);
    float dmin = ATM_TOP - r, dmax = rho + H;
    float d = dmin + uv.x * (dmax - dmin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

// Transmittance from radius r (km) toward the top of the atmosphere along a
// direction with cosine mu to the local vertical. Zero if the earth is in the
// way - which is what turns the clouds' undersides red after sunset.
vec3 transmittance(float r, float mu) {
    float cos_h = -sqrt(max(0.0, 1.0 - (ATM_BOTTOM * ATM_BOTTOM) / (r * r)));
    vec3 t = texture(uTransLUT, trans_uv(r, mu)).rgb;
    // soft terminator rather than a hard cut at the geometric horizon
    return t * smoothstep(cos_h - 0.01, cos_h + 0.002, mu);
}

vec3 multi_scatter(float r, float mu_s) {
    vec2 uv = vec2(mu_s * 0.5 + 0.5, (r - ATM_BOTTOM) / (ATM_TOP - ATM_BOTTOM));
    return texture(uMultiLUT, uv).rgb;
}

// --- the sky-view LUT's parameterisation -----------------------------------
// u: world azimuth (0 = north, clockwise), wrapping. v: zenith angle, packed
// tight around the horizon where the sky changes fastest.
vec2 skyview_uv(vec3 dir, float r) {
    float vh = sqrt(max(r * r - ATM_BOTTOM * ATM_BOTTOM, 0.0));
    float cos_beta = vh / r;
    float beta = acos(clamp(cos_beta, -1.0, 1.0));
    float zen_h = PI - beta;
    float zen = acos(clamp(dir.y, -1.0, 1.0));
    float v;
    if (zen < zen_h) {
        float c = zen / zen_h;
        c = 1.0 - c;
        c = sqrt(max(c, 0.0));
        c = 1.0 - c;
        v = c * 0.5;
    } else {
        float c = (zen - zen_h) / max(beta, 1e-4);
        c = sqrt(max(c, 0.0));
        v = c * 0.5 + 0.5;
    }
    float az = atan(dir.x, -dir.z);          // clockwise from north
    float u = az / (2.0 * PI) + 0.5;
    return vec2(u, v);
}
vec3 skyview_dir(vec2 uv, float r) {
    float vh = sqrt(max(r * r - ATM_BOTTOM * ATM_BOTTOM, 0.0));
    float cos_beta = vh / r;
    float beta = acos(clamp(cos_beta, -1.0, 1.0));
    float zen_h = PI - beta;
    float zen;
    if (uv.y < 0.5) {
        float c = 2.0 * uv.y;
        c = 1.0 - c;
        c *= c;
        c = 1.0 - c;
        zen = zen_h * c;
    } else {
        float c = uv.y * 2.0 - 1.0;
        c *= c;
        zen = zen_h + beta * c;
    }
    float az = (uv.x - 0.5) * 2.0 * PI;
    float s = sin(zen);
    return vec3(s * sin(az), cos(zen), -s * cos(az));
}

// --- the aerial perspective volume -----------------------------------------
// 32 x 32 screen cells x 32 depth slices, laid side by side in a 1024 x 32
// texture. Slice k holds the scattering from the camera out to
// AP_MAX * ((k + 1) / 32)^2 km.
const float AP_MAX_KM = 450.0;
const float AP_SLICES = 32.0;

float ap_slice_dist(float k) { float s = (k + 1.0) / AP_SLICES; return AP_MAX_KM * s * s; }

uniform sampler2D uAerialLUT;     // in-scattering
uniform sampler2D uAerialTLUT;    // transmittance, per channel

// In-scattering and transmittance between the camera and a point `dist_m`
// away at screen position `uv`. Apply as colour * T + S.
void aerial(vec2 uv, float dist_m, out vec3 S, out vec3 T) {
    float d = dist_m * 0.001;
    float s = sqrt(max(d, 0.0) / AP_MAX_KM) * AP_SLICES - 1.0;
    uv = clamp(uv, 0.5 / 32.0, 1.0 - 0.5 / 32.0);
    if (s <= 0.0) {
        vec2 c = vec2(uv.x / AP_SLICES, uv.y);
        float w = saturate(s + 1.0);
        S = texture(uAerialLUT, c).rgb * w;
        T = mix(vec3(1.0), texture(uAerialTLUT, c).rgb, w);
        return;
    }
    s = min(s, AP_SLICES - 1.0);
    float k0 = floor(s), k1 = min(k0 + 1.0, AP_SLICES - 1.0);
    vec2 c0 = vec2((uv.x + k0) / AP_SLICES, uv.y), c1 = vec2((uv.x + k1) / AP_SLICES, uv.y);
    float f = s - k0;
    S = mix(texture(uAerialLUT, c0).rgb, texture(uAerialLUT, c1).rgb, f);
    T = mix(texture(uAerialTLUT, c0).rgb, texture(uAerialTLUT, c1).rgb, f);
}
