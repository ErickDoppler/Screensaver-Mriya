// A panel hanging in the world: the menu, and the pointer's ray and dot.
// The quad's corners come from the instance, so one draw call does the lot.
layout(location = 0) in vec4 aA;    // xyz corner 0 (camera-relative), w unused
layout(location = 1) in vec4 aU;    // xyz the panel's width axis, times its width
layout(location = 2) in vec4 aV;    // xyz its height axis, times its height; w = kind

uniform mat4 uViewProj;
out vec2 vUV;
out float vKind;

void main() {
    vec2 c = vec2((gl_VertexID & 1) == 0 ? 0.0 : 1.0, (gl_VertexID & 2) == 0 ? 0.0 : 1.0);
    vec3 p = aA.xyz + aU.xyz * (c.x - 0.5) + aV.xyz * (c.y - 0.5);
    gl_Position = uViewProj * vec4(p, 1.0);
    /* the page is drawn with y running down, into a texture whose origin is
     * at the bottom: the two cancel, so no flip belongs here */
    vUV = c;
    vKind = aV.w;
}
