// Aerial perspective volume: in-scattering and transmittance from the camera
// to each of 32 depths, for 32 x 32 directions across the view.
in vec2 vUV;
layout(location = 0) out vec4 fragS;
layout(location = 1) out vec4 fragT;

uniform float uCamR;          // km
uniform vec3  uSunDir, uSunIllum, uMoonDir, uMoonIllum;
uniform mat3  uCamBasis;      // right, up, back
uniform vec2  uTanHalf;       // tan(fov/2) * aspect, tan(fov/2)

void main() {
    // which slice and which screen cell this texel is
    float x = vUV.x * AP_SLICES;
    float k = floor(x);
    vec2 uv = vec2(x - k, vUV.y);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rd = normalize(uCamBasis * vec3(ndc.x * uTanHalf.x, ndc.y * uTanHalf.y, -1.0));
    float dist = ap_slice_dist(k);

    vec3 ro = vec3(0.0, uCamR, 0.0);
    // stop at the ground: past it there is nothing to see through anyway
    vec2 gnd = ray_sphere(ro, rd, ATM_BOTTOM);
    if (gnd.x > 0.0) dist = min(dist, gnd.x);

    float cs = dot(rd, uSunDir), cm = dot(rd, uMoonDir);
    float prs = phase_rayleigh(cs), pms = phase_mie(cs, uMieG);
    float prm = phase_rayleigh(cm), pmm = phase_mie(cm, uMieG);
    int N = int(clamp(4.0 + k * 0.6, 4.0, 20.0));
    float dt = dist / float(N);
    vec3 T = vec3(1.0), L = vec3(0.0);
    for (int i = 0; i < N; ++i) {
        vec3 p = ro + rd * (float(i) + 0.5) * dt;
        float r = length(p);
        vec3 up = p / r;
        Medium m = medium_at(r - ATM_BOTTOM);
        float mus = dot(up, uSunDir), mum = dot(up, uMoonDir);
        vec3 s = ((m.scat_r * prs + m.scat_m * pms) * transmittance(r, mus) +
                  (m.scat_r + m.scat_m) * multi_scatter(r, mus)) * uSunIllum;
        s += ((m.scat_r * prm + m.scat_m * pmm) * transmittance(r, mum) +
              (m.scat_r + m.scat_m) * multi_scatter(r, mum)) * uMoonIllum;
        vec3 step_t = exp(-m.ext * dt);
        L += T * s * (vec3(1.0) - step_t) / max(m.ext, vec3(1e-6));
        T *= step_t;
    }
    fragS = vec4(L, 1.0);
    fragT = vec4(T, 1.0);
}
