// kind 0: the menu's page. kind 1: the pointer's ray, a soft line.
in vec2 vUV;
in float vKind;
out vec4 frag;

uniform sampler2D uPage;
uniform float uAlpha;

void main() {
    if (vKind < 0.5) {
        vec4 c = texture(uPage, vUV);
        frag = vec4(c.rgb, c.a * uAlpha);
    } else {
        // the ray: brightest along its middle, fading out at its far end
        float across = abs(vUV.x - 0.5) * 2.0;
        float a = (1.0 - smoothstep(0.2, 1.0, across)) * (1.0 - vUV.y * 0.75) * uAlpha;
        frag = vec4(vec3(0.55, 1.0, 0.7) * a, a);
    }
}
