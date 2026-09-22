in vec2 vQuad;
in vec3 vCol;
out vec4 frag;
void main() {
    float r2 = dot(vQuad, vQuad);
    float g = exp(-r2 * 5.0);
    frag = vec4(vCol * g, 0.0);
}
