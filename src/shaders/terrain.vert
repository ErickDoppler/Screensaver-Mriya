// One level of the terrain grid. The grid is a fixed (N+1) x (N+1) lattice of
// integer coordinates; each level places it at its own spacing, snapped to
// the world, and near its outer edge slides every odd vertex onto its even
// neighbour so the rim matches the coarser level outside it exactly.
layout(location = 0) in vec2 aGrid;

uniform vec2  uOriginRel;     // level vertex (0,0), relative to the camera (x, z)
uniform vec2  uOriginWorld;   // and in the world
uniform float uSpacing;
uniform float uGridN;
uniform mat4  uViewProj;      // rotation + projection, camera at the origin

out vec3 vRel;
out vec2 vWorld;
out float vH;

void main() {
    vec2 g = aGrid;
    vec2 c = abs(g - uGridN * 0.5) / (uGridN * 0.5);
    float m = smoothstep(0.66, 0.9, max(c.x, c.y));
    vec2 gm = g - mod(g, 2.0) * m;
    vec2 world = uOriginWorld + gm * uSpacing;
    vec2 rel = uOriginRel + gm * uSpacing;
    float h = terrain_height(world, uSpacing * mix(2.0, 4.0, m));
    float d2 = dot(rel, rel);
    // the earth curves away: a point 100 km off sits 785 m lower
    float y = max(h, 0.0) - uCamWorld.y - d2 / (2.0 * EARTH_R);
    vRel = vec3(rel.x, y, rel.y);
    vWorld = world;
    vH = h;
    gl_Position = uViewProj * vec4(vRel, 1.0);
}
