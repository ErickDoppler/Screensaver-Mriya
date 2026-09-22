in vec2 vQuad;
in float vAlpha;
out vec4 frag;

uniform sampler2D uProbe;
uniform float uSnow;
uniform vec3  uFlashCol;
uniform vec4  uPointPos[4];
uniform vec3  uPointCol[4];

void main() {
    float across = 1.0 - abs(vQuad.y);
    float a = vAlpha * across * mix(0.35, 1.0, uSnow);
    // a streak brightens toward its head
    a *= mix(0.4, 1.0, vQuad.x);
    vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb + texelFetch(uProbe, ivec2(2, 0), 0).rgb * 0.5;
    vec3 col = amb * mix(1.2, 2.2, uSnow) + uFlashCol;
    // near the aircraft, its lights catch the drops
    for (int l = 0; l < 4; ++l) col += uPointCol[l] * uPointPos[l].w * 0.004;
    frag = vec4(col * a, a);
}
