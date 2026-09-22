// Log luminance of the picture, weighted toward the centre, for the eye's
// adaptation. Mipmapped down to one texel afterwards.
in vec2 vUV;
out vec4 frag;
uniform sampler2D uTex;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float l = max(luma(c), 1e-5);
    vec2 d = vUV - 0.5;
    float w = 1.0 - dot(d, d) * 1.6;
    // the sun's disc would drag everything dark; clamp the brightest
    frag = vec4(log2(min(l, 60.0)) * w, w, 0.0, 1.0);
}
