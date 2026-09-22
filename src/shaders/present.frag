// The last pass: the finished picture onto the screen, with grain and the
// fade for camera cuts. (The edges are smoothed earlier, by multisampling
// the scene - see msresolve.frag - which leaves the textures sharp.)
in vec2 vUV;
out vec4 frag;

uniform sampler2D uLDR;       // the tone-mapped picture, sRGB
uniform float uTime;
uniform float uNight;
uniform float uFade;

void main() {
    vec3 c = texelFetch(uLDR, ivec2(gl_FragCoord.xy), 0).rgb;
    // grain, which also breaks up banding in the sky
    c += (ign(gl_FragCoord.xy, uTime * 60.0) - 0.5) * (1.5 / 255.0 + uNight * 3.0 / 255.0);
    c *= 1.0 - uFade;
    frag = vec4(c, 1.0);
}
