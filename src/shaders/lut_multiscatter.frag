// Multiple-scattering LUT (Hillaire 2020, section 5.5): the light that has
// bounced more than once, approximated as isotropic and summed as a
// geometric series. 32 x 32: sun zenith cosine by height.
in vec2 vUV;
out vec4 frag;

uniform vec3 uGroundAlbedo;

void main() {
    float mu_s = vUV.x * 2.0 - 1.0;
    float r = ATM_BOTTOM + vUV.y * (ATM_TOP - ATM_BOTTOM);
    r = clamp(r, ATM_BOTTOM + 0.01, ATM_TOP - 0.01);
    vec3 sun = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), mu_s, 0.0);
    vec3 ro = vec3(0.0, r, 0.0);

    const int DIRS = 8;          // 8 x 8 directions over the sphere
    const int N = 20;
    vec3 lum_total = vec3(0.0), fms = vec3(0.0);
    for (int i = 0; i < DIRS; ++i)
    for (int j = 0; j < DIRS; ++j) {
        float th = PI * (float(i) + 0.5) / float(DIRS);
        float ph = 2.0 * PI * (float(j) + 0.5) / float(DIRS);
        vec3 rd = vec3(sin(th) * cos(ph), cos(th), sin(th) * sin(ph));
        vec2 top = ray_sphere(ro, rd, ATM_TOP);
        vec2 gnd = ray_sphere(ro, rd, ATM_BOTTOM);
        float len = top.y;
        bool hit_ground = gnd.x > 0.0;
        if (hit_ground) len = gnd.x;
        float dt = len / float(N);
        vec3 T = vec3(1.0), L = vec3(0.0), F = vec3(0.0);
        for (int k = 0; k < N; ++k) {
            vec3 p = ro + rd * (float(k) + 0.5) * dt;
            float pr = length(p);
            Medium m = medium_at(pr - ATM_BOTTOM);
            vec3 up = p / pr;
            vec3 ts = transmittance(pr, dot(sun, up));
            vec3 scat = m.scat_r + vec3(m.scat_m);
            vec3 step_t = exp(-m.ext * dt);
            vec3 integ = (vec3(1.0) - step_t) / max(m.ext, vec3(1e-6));
            // isotropic phase for the multiply-scattered estimate
            L += T * scat * ts * (1.0 / (4.0 * PI)) * integ;
            F += T * scat * integ;
            T *= step_t;
        }
        if (hit_ground) {
            vec3 p = ro + rd * len;
            vec3 up = normalize(p);
            L += T * transmittance(length(p), dot(sun, up)) * saturate(dot(up, sun)) * uGroundAlbedo / PI;
        }
        float w = sin(th) * (PI / float(DIRS)) * (2.0 * PI / float(DIRS)) / (4.0 * PI);
        lum_total += L * w;
        fms += F * w;
    }
    vec3 psi = lum_total / max(vec3(1.0) - fms, vec3(1e-3));
    frag = vec4(psi, 1.0);
}
