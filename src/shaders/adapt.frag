// The eye adapting: eases the remembered average luminance toward the
// current one, faster going bright than going dark, as eyes do.
out vec4 frag;
uniform sampler2D uLum;
uniform sampler2D uPrev;
uniform float uDt;
uniform float uLevels;
uniform float uReset;
void main() {
    vec2 s = textureLod(uLum, vec2(0.5), uLevels).rg;
    float avg = exp2(s.x / max(s.y, 1e-4));
    float prev = texelFetch(uPrev, ivec2(0), 0).r;
    if (uReset > 0.5 || !(prev > 0.0) || isinf(prev)) prev = avg;
    float tau = avg > prev ? 0.9 : 2.2;
    float a = 1.0 - exp(-uDt / tau);
    frag = vec4(exp(mix(log(prev), log(avg), a)), 0.0, 0.0, 1.0);
}
