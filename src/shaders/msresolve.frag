// Folds the multisampled scene into one sample a pixel: the colour is the
// average of the samples - which is what smooths the edges - and the
// distance is the nearest of them, never an average: half the aircraft and
// half the sky do not make something halfway out, and the clouds behind
// must not show through the edge.
in vec2 vUV;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragDist;

uniform sampler2DMS uColorMS;
uniform sampler2DMS uDistMS;
uniform int uSamples;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = vec4(0.0);
    float d = 1e30;
    for (int i = 0; i < uSamples; ++i) {
        c += texelFetch(uColorMS, p, i);
        d = min(d, texelFetch(uDistMS, p, i).r);
    }
    fragColor = c / float(uSamples);
    fragDist = vec4(d, 0.0, 0.0, 1.0);
}
