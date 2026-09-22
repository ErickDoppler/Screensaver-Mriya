// Transmittance LUT: optical depth from any height to the top of the
// atmosphere in any direction, integrated once per change of air.
in vec2 vUV;
out vec4 frag;

void main() {
    float r, mu;
    trans_params(vUV, r, mu);
    vec3 ro = vec3(0.0, r, 0.0);
    vec3 rd = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
    vec2 top = ray_sphere(ro, rd, ATM_TOP);
    float len = max(top.y, 0.0);
    const int N = 48;
    vec3 od = vec3(0.0);
    float dt = len / float(N);
    for (int i = 0; i < N; ++i) {
        vec3 p = ro + rd * (float(i) + 0.5) * dt;
        od += medium_at(length(p) - ATM_BOTTOM).ext * dt;
    }
    frag = vec4(exp(-od), 1.0);
}
