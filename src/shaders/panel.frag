// kind 0: the menu's page. kind 1: the pointer's ray, a soft line.
in vec2 vUV;
in float vKind;
out vec4 frag;

uniform sampler2D uPage, uPage2;
uniform float uAlpha, uAlpha2;

void main() {
    if (vKind < 0.5) {
        /* the instruments: brightened, the way a head-up display is, so they
         * hold up against a bright sky behind them */
        vec4 c = texture(uPage, vUV);
        frag = vec4(c.rgb * 1.7, min(c.a * uAlpha * 1.35, 1.0));
    } else if (vKind < 1.5) {
        vec4 c = texture(uPage2, vUV);
        frag = vec4(c.rgb, c.a * uAlpha2);
    } else {
        // the ray: brightest along its middle, fading out at its far end
        float across = abs(vUV.x - 0.5) * 2.0;
        float a = (1.0 - smoothstep(0.2, 1.0, across)) * (1.0 - vUV.y * 0.75) * uAlpha;
        frag = vec4(vec3(0.55, 1.0, 0.7) * a, a);
    }
}
