// Temporal accumulation for the clouds. Each frame marches every pixel once
// with a different jitter; this blends the new result into the history,
// reprojected to where the same bit of cloud was on screen last frame (the
// aircraft moves 200 m a second, so "same pixel" would smear), with the
// history clamped to what the current neighbourhood allows so a cloud that
// moves off a pixel does not leave a ghost.
in vec2 vUV;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragDepth;

uniform sampler2D uCur;        // this frame's clouds: rgb scattered light, a transmittance
uniform sampler2D uCurDepth;   // where along the ray they are, metres
uniform sampler2D uHist;
uniform sampler2D uHistDepth;
uniform mat3  uCamBasis;
uniform vec2  uTanHalf;
uniform mat3  uPrevBasis;
uniform vec2  uPrevTanHalf;
uniform vec3  uCamDelta;       // this camera minus last frame's, world metres
uniform float uBlend;          // weight of the history, 0 resets
uniform vec2  uRes;

void main() {
    vec4 cur = texture(uCur, vUV);
    float d = texture(uCurDepth, vUV).r;
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 rd = normalize(uCamBasis * vec3(ndc.x * uTanHalf.x, ndc.y * uTanHalf.y, -1.0));
    // the point this pixel sees, relative to last frame's camera
    vec3 p = rd * d + uCamDelta;
    vec3 v = transpose(uPrevBasis) * p;           // into last frame's camera space
    vec4 res = cur;
    float hd = d;
    if (v.z < -1.0 && uBlend > 0.0) {
        vec2 pn = vec2(v.x / (-v.z * uPrevTanHalf.x), v.y / (-v.z * uPrevTanHalf.y));
        vec2 puv = pn * 0.5 + 0.5;
        if (all(greaterThan(puv, vec2(0.0))) && all(lessThan(puv, vec2(1.0)))) {
            vec4 hist = texture(uHist, puv);
            // the neighbourhood's range: history outside it is stale
            vec2 px = 1.0 / uRes;
            vec4 mn = cur, mx = cur;
            for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec4 s = texture(uCur, vUV + vec2(x, y) * px);
                mn = min(mn, s);
                mx = max(mx, s);
            }
            // a little slack, or the clamp reintroduces the noise it removes
            vec4 slack = (mx - mn) * 0.25;
            hist = clamp(hist, mn - slack, mx + slack);
            res = mix(cur, hist, uBlend);
            hd = mix(d, texture(uHistDepth, puv).r, uBlend * 0.5);
        }
    }
    fragColor = res;
    fragDepth = vec4(hd, 0.0, 0.0, 1.0);
}
