// Folds the multisampled scene into one sample a pixel: the colour is the
// average of the samples - which is what smooths the edges - and the
// distance is the nearest of them, never an average: half the aircraft and
// half the sky do not make something halfway out. With it goes the share
// of the pixel that lies well beyond that nearest surface - the background
// part of an edge - so the clouds behind can still be laid over that part.
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
    float ds[8];
    int n = min(uSamples, 8);
    for (int i = 0; i < n; ++i) {
        c += texelFetch(uColorMS, p, i);
        ds[i] = texelFetch(uDistMS, p, i).r;
        d = min(d, ds[i]);
    }
    float far = 0.0;
    for (int i = 0; i < n; ++i) far += step(d * 1.5 + 5.0, ds[i]);
    fragColor = c / float(n);
    fragDist = vec4(d, far / float(n), 0.0, 1.0);
}
