// Sky-view LUT: the sky's radiance in every direction as seen from the
// camera's height, for both the sun and the moon, redrawn every frame.
in vec2 vUV;
out vec4 frag;

uniform float uCamR;          // km from the earth's centre
uniform vec3  uSunDir;
uniform vec3  uSunIllum;      // the sun's illuminance outside the atmosphere
uniform vec3  uMoonDir;
uniform vec3  uMoonIllum;

vec3 scatter_along(vec3 ro, vec3 rd, float len, vec3 ldir, vec3 lillum) {
    const int N = 32;
    float cos_t = dot(rd, ldir);
    float pr = phase_rayleigh(cos_t), pm = phase_mie(cos_t, uMieG);
    vec3 T = vec3(1.0), L = vec3(0.0);
    float t0 = 0.0;
    for (int i = 0; i < N; ++i) {
        // denser steps near the camera, where the air is thickest
        float t1 = len * pow((float(i) + 1.0) / float(N), 1.6);
        float dt = t1 - t0;
        vec3 p = ro + rd * (t0 + 0.5 * dt);
        t0 = t1;
        float r = length(p);
        vec3 up = p / r;
        float mu_l = dot(up, ldir);
        Medium m = medium_at(r - ATM_BOTTOM);
        vec3 tl = transmittance(r, mu_l);
        vec3 ms = multi_scatter(r, mu_l);
        vec3 s = (m.scat_r * pr + m.scat_m * pm) * tl + (m.scat_r + m.scat_m) * ms;
        vec3 step_t = exp(-m.ext * dt);
        L += T * s * (vec3(1.0) - step_t) / max(m.ext, vec3(1e-6));
        T *= step_t;
    }
    return L * lillum;
}

void main() {
    vec3 rd = skyview_dir(vUV, uCamR);
    vec3 ro = vec3(0.0, uCamR, 0.0);
    vec2 top = ray_sphere(ro, rd, ATM_TOP);
    vec2 gnd = ray_sphere(ro, rd, ATM_BOTTOM);
    float len = max(top.y, 0.0);
    if (gnd.x > 0.0) len = gnd.x;
    vec3 L = scatter_along(ro, rd, len, uSunDir, uSunIllum);
    if (dot(uMoonIllum, vec3(1.0)) > 0.0)
        L += scatter_along(ro, rd, len, uMoonDir, uMoonIllum);
    frag = vec4(L, 1.0);
}
