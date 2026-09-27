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
    // How far out this vertex is, measured from the camera itself and in the
    // square metric the levels are laid out in. Taken from the grid instead,
    // it is measured from the level's snapped origin, which moves in whole
    // steps - and then the blend below, and the detail, jump with it.
    vec2 rel0 = uOriginRel + g * uSpacing;
    float half_ext = uGridN * 0.5 * uSpacing;
    float cheb = max(abs(rel0.x), abs(rel0.y));
    float m = smoothstep(0.66, 0.9, cheb / half_ext);
    vec2 gm = g - mod(g, 2.0) * m;
    vec2 world = uOriginWorld + gm * uSpacing;
    vec2 rel = uOriginRel + gm * uSpacing;
    float d2 = dot(rel, rel);
    // How fine the ground may be here: from the same distance, so it too
    // moves smoothly. 8 / gridN reproduces exactly the detail each level had
    // (a level spans 32 to 64 of its own spacings out from the camera, so
    // this is 2 x spacing at its inner edge and 4 x at its rim), and two
    // neighbouring levels work out the same value along their shared edge.
    float mw = max(cheb * 8.0 / uGridN, uSpacing * 2.0);
    float h = terrain_height(world, mw);
    // the earth curves away: a point 100 km off sits 785 m lower
    float y = max(h, 0.0) - uCamWorld.y - d2 / (2.0 * EARTH_R);
    vRel = vec3(rel.x, y, rel.y);
    vWorld = world;
    vH = h;
    gl_Position = uViewProj * vec4(vRel, 1.0);
}
