/* The ground: biome parameters, the height function the flight model needs,
 * and the camera-centred grid levels the renderer draws it with.
 *
 * The height function is a line-for-line copy of shaders/terrain_fn.glsl. The
 * GPU draws it; the CPU only asks it how high the ground is ahead, so the
 * aircraft can keep clear of it. */
#ifndef MR_TERRAIN_H
#define MR_TERRAIN_H
#include "mathx.h"

enum {
    BIOME_FARMLAND = 0,   /* the steppe: a patchwork of huge fields, rivers */
    BIOME_OCEAN,          /* open sea with the odd island */
    BIOME_MOUNTAINS,      /* alpine ranges, snow on the peaks, lakes */
    BIOME_DESERT,         /* dunes and bare rock ranges */
    BIOME_ARCTIC,         /* snow, ice and sea ice */
    BIOME_TROPICAL,       /* islands and reefs in turquoise water */
    BIOME_FOREST,         /* taiga and lakes */
    BIOME_COAST,          /* a coastline: fields meeting the sea */
    BIOME_HIMALAYA,       /* the roof of the world: 8 km peaks over a high plateau */
    BIOME_COUNT
};

typedef struct TerrainParams {
    int   biome;
    /* While the scenery changes: the ground it is turning into, and how far
     * it has come (0..1). The heights and the land cover are blended, never
     * the parameters - the scales above all: a feature 500 km from the origin
     * would slide past at kilometres a second as its scale changed. */
    const struct TerrainParams *to;
    int   biome2;
    float biome_mix;
    unsigned seed;
    float base, cont_amp, cont_scale_km, hill_amp;
    float hill_scale_km, mtn_amp, mtn_scale_km, mtn_mask_lo;
    float mtn_mask_hi, river, river_scale_km, dune_amp;
    float snow_line, tree_line;
    float field_km, forest, towns, snow_cover;
} TerrainParams;

void  terrain_params(TerrainParams *t, int biome, unsigned seed);
/* Height in metres at world (x, z). min_wave drops detail finer than that,
 * in metres; the flight model asks for ~150 m. */
float terrain_height(const TerrainParams *t, double x, double z, float min_wave);
/* The highest ground within `radius` metres of a point, sampled coarsely. */
float terrain_max_near(const TerrainParams *t, double x, double z, float radius);
const char *terrain_biome_name(int biome);

/* --- the grid ------------------------------------------------------------
 * Nested square levels, each twice the spacing of the one inside it, all
 * centred on the camera and snapped to their own grid so the vertices never
 * slide over the ground as the aircraft moves. */
#define TERRAIN_GRID 128          /* cells along a level's side */
#define TERRAIN_MAX_LEVELS 14

typedef struct TerrainLevel {
    double origin_x, origin_z;    /* world position of grid vertex (0, 0) */
    float  spacing;               /* metres between vertices */
} TerrainLevel;

typedef struct TerrainGrid {
    int levels;
    TerrainLevel level[TERRAIN_MAX_LEVELS];
} TerrainGrid;

/* Lays the levels out around the camera. `agl` (height above the ground)
 * picks the finest spacing, and levels are added until they reach `reach`. */
void terrain_grid_layout(TerrainGrid *g, double cam_x, double cam_z, float agl, float reach);
#endif
