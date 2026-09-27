// One puff of a contrail: a camera-facing quad, many of them overlapping
// along each engine's trail.
layout(location = 0) in vec4 aPos;    // xyz centre (camera-relative), w radius
layout(location = 1) in vec4 aInfo;   // opacity, seed, age, spare

uniform mat4 uViewProj;
uniform mat3 uCamBasis;               // columns: right, up, back
uniform vec3 uLightDir;               // the way the light travels

out vec2  vQuad;      // -1..1 across the puff
out float vAlpha;
out float vSeed;
out float vAge;
out vec2  vSunXY;     // where the sun is, in the quad's own axes
out vec3  vRel;

void main() {
    int corner = gl_VertexID;
    vec2 q = vec2((corner & 1) == 0 ? -1.0 : 1.0, (corner & 2) == 0 ? -1.0 : 1.0);
    vec3 right = vec3(uCamBasis[0][0], uCamBasis[0][1], uCamBasis[0][2]);
    vec3 up    = vec3(uCamBasis[1][0], uCamBasis[1][1], uCamBasis[1][2]);
    vec3 c = aPos.xyz + (right * q.x + up * q.y) * aPos.w;
    gl_Position = uViewProj * vec4(c, 1.0);
    vQuad = q;
    vAlpha = aInfo.x;
    vSeed = aInfo.y;
    vAge = aInfo.z;
    // the sun's direction flattened onto the quad: which side of each puff
    // is lit, and which is in its own shadow
    vec2 sxy = vec2(dot(-uLightDir, right), dot(-uLightDir, up));
    vSunXY = length(sxy) > 1e-4 ? normalize(sxy) : vec2(0.0, 1.0);
    vRel = aPos.xyz;
}
