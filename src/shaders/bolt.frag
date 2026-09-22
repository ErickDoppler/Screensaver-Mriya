in vec2 vQuad;
in float vBright;
out vec4 frag;
uniform float uFlash;
uniform float uGlow;
void main() {
    float x = abs(vQuad.y);
    float core = uGlow > 0.5 ? exp(-x * x * 4.0) * 0.08 : exp(-x * x * 6.0);
    vec3 col = vec3(0.8, 0.85, 1.0) * core * vBright * uFlash * 40.0;
    frag = vec4(col, 0.0);
}
