// The aircraft's navigation lights, beacons and strobes as small glowing
// sprites; the bloom does the rest.
uniform mat4 uViewProj;
uniform vec4 uLightPos[8];      // camera-relative xyz, w = size in metres
uniform vec4 uLightCol[8];      // rgb intensity, a = on
uniform vec2 uRes;
out vec2 vQuad;
out vec3 vCol;

void main() {
    int i = gl_InstanceID;
    int corner = gl_VertexID;
    vec2 q = vec2((corner & 1) == 0 ? -1.0 : 1.0, (corner & 2) == 0 ? -1.0 : 1.0);
    vec4 c = uViewProj * vec4(uLightPos[i].xyz, 1.0);
    // at least a few pixels across, however far away
    float px = max(uLightPos[i].w / max(c.w, 0.1) * uRes.y * 0.9, 3.0);
    vec2 off = q * px / uRes * 2.0 * c.w;
    gl_Position = vec4(c.xy + off, c.z - 0.002 * c.w, c.w);
    // a light at or behind the lens (the wingtip camera sits beside two)
    if (c.w < 1.5) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vQuad = q;
    vCol = uLightCol[i].rgb * uLightCol[i].a * (uLightPos[i].w / max(px / uRes.y * c.w, 1e-4) * 0.3);
}
