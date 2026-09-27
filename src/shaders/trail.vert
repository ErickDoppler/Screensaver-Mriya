// A contrail: one continuous ribbon per engine, its vertices already placed
// to either side of the trail by the renderer (a strip, so no seam at a join).
layout(location = 0) in vec4 aPos;    // xyz camera-relative, w side (-1 / +1)
layout(location = 1) in vec4 aInfo;   // age (s), opacity, engine, half-width

uniform mat4 uViewProj;

out float vAcross;    // -1 at one edge, +1 at the other
out float vAlpha;
out float vAge;
out float vSeed;
out float vWidth;
out vec3  vRel;

void main() {
    gl_Position = uViewProj * vec4(aPos.xyz, 1.0);
    vAcross = aPos.w;
    vAge = aInfo.x;
    vAlpha = aInfo.y;
    vSeed = aInfo.z;
    vWidth = aInfo.w;
    vRel = aPos.xyz;
}
