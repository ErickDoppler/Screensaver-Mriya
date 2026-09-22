// Contrail: a soft white plume, brightest looking toward the sun (ice
// crystals scatter forward), greyer on the shadowed side, ragged along its
// length as it spreads.
in vec2 vQuad;
in float vAlpha;
in float vAge;
in vec3 vRel;
out vec4 frag;

uniform sampler2D uProbe;
uniform vec3 uLightDir;
uniform float uTime;

void main() {
    float across = abs(vQuad.y);
    float soft = exp(-across * across * 3.0);
    // puffs along the trail, from the exhaust vortices breaking up
    float puff = 0.75 + 0.25 * sin(vAge * 9.0 + vRel.x * 0.02) * sin(vAge * 3.7 + 1.3);
    float a = vAlpha * soft * puff;
    vec3 V = normalize(-vRel);
    float c = dot(-V, uLightDir);
    vec3 key = texelFetch(uProbe, ivec2(3, 0), 0).rgb * texelFetch(uProbe, ivec2(0, 0), 0).r;
    vec3 amb = texelFetch(uProbe, ivec2(1, 0), 0).rgb;
    vec3 col = key * (hg(c, 0.7) * 0.6 + 0.08) + amb * 0.9;
    frag = vec4(col * a, a);
}
