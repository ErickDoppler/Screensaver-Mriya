#include "terrain.h"
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Biomes. Heights in metres, scales in kilometres. The flight's soft floor
 * follows the ground (see flight.c), so the mountains are allowed to be real
 * mountains - up to the Himalaya's.
 * ------------------------------------------------------------------------- */
void terrain_params(TerrainParams *t, int biome, unsigned seed) {
    memset(t, 0, sizeof *t);
    t->biome = biome;
    t->biome2 = biome;
    t->biome_mix = 0.f;
    t->to = NULL;
    t->seed = seed & 0xFFFFFFu;        /* travels to the GPU as an exact float */
    t->cont_scale_km = 160.f;
    t->hill_scale_km = 9.f;
    t->mtn_scale_km = 28.f;
    t->mtn_mask_lo = 0.0f;
    t->mtn_mask_hi = 0.35f;
    t->river_scale_km = 26.f;
    t->snow_line = 99999.f;
    t->tree_line = 1900.f;
    t->field_km = 0.9f;
    t->forest = 0.25f;
    t->towns = 0.5f;
    switch (biome) {
    case BIOME_OCEAN:
        t->base = -1400.f; t->cont_amp = 2300.f; t->cont_scale_km = 110.f;
        t->hill_amp = 120.f; t->mtn_amp = 700.f; t->mtn_mask_lo = 0.1f; t->mtn_mask_hi = 0.4f;
        t->forest = 0.45f; t->towns = 0.2f; t->snow_line = 3500.f;
        break;
    case BIOME_MOUNTAINS:
        t->base = 500.f; t->cont_amp = 350.f; t->hill_amp = 140.f;
        t->mtn_amp = 3300.f; t->mtn_scale_km = 32.f; t->mtn_mask_lo = -0.35f; t->mtn_mask_hi = 0.15f;
        t->river = 0.8f; t->river_scale_km = 20.f;
        t->snow_line = 2300.f; t->tree_line = 1800.f; t->forest = 0.55f; t->towns = 0.25f;
        t->field_km = 0.35f;
        break;
    case BIOME_HIMALAYA:
        /* ranges whose peaks reach 7-8.5 km over deep valleys two km up and a
         * high plateau between; dry grass to 5 km, snow from 5.6 */
        t->base = 1700.f; t->cont_amp = 900.f; t->cont_scale_km = 120.f; t->hill_amp = 180.f;
        t->mtn_amp = 7500.f; t->mtn_scale_km = 38.f; t->mtn_mask_lo = -0.6f; t->mtn_mask_hi = -0.05f;
        /* no carved rivers: in valleys this deep their narrow trenches cut
         * sheer walls hundreds of metres high that no mesh can follow */
        t->snow_line = 5600.f; t->tree_line = 5000.f; t->forest = 0.2f; t->towns = 0.04f;
        t->field_km = 0.3f;
        break;
    case BIOME_DESERT:
        t->base = 380.f; t->cont_amp = 260.f; t->hill_amp = 70.f;
        t->mtn_amp = 1100.f; t->mtn_scale_km = 22.f; t->mtn_mask_lo = 0.15f; t->mtn_mask_hi = 0.45f;
        t->dune_amp = 55.f; t->forest = 0.f; t->towns = 0.08f;
        break;
    case BIOME_ARCTIC:
        t->base = -60.f; t->cont_amp = 380.f; t->cont_scale_km = 90.f; t->hill_amp = 90.f;
        t->mtn_amp = 900.f; t->mtn_mask_lo = 0.1f; t->mtn_mask_hi = 0.45f;
        t->snow_line = -9999.f; t->forest = 0.f; t->towns = 0.02f; t->snow_cover = 1.f;
        break;
    case BIOME_TROPICAL:
        t->base = -700.f; t->cont_amp = 1150.f; t->cont_scale_km = 70.f; t->hill_amp = 90.f;
        t->mtn_amp = 900.f; t->mtn_scale_km = 12.f; t->mtn_mask_lo = 0.0f; t->mtn_mask_hi = 0.3f;
        t->forest = 0.8f; t->towns = 0.15f;
        break;
    case BIOME_FOREST:
        t->base = 70.f; t->cont_amp = 110.f; t->cont_scale_km = 60.f; t->hill_amp = 55.f;
        t->river = 1.2f; t->river_scale_km = 14.f;
        t->forest = 0.85f; t->towns = 0.12f; t->field_km = 0.4f;
        break;
    case BIOME_COAST:
        t->base = 20.f; t->cont_amp = 700.f; t->cont_scale_km = 140.f; t->hill_amp = 70.f;
        t->mtn_amp = 800.f; t->mtn_mask_lo = 0.25f; t->mtn_mask_hi = 0.5f;
        t->river = 1.f; t->forest = 0.3f; t->towns = 0.7f;
        break;
    default: /* BIOME_FARMLAND */
        t->base = 170.f; t->cont_amp = 140.f; t->hill_amp = 55.f;
        t->river = 1.f; t->forest = 0.18f; t->towns = 0.6f; t->field_km = 1.1f;
        break;
    }
}

const char *terrain_biome_name(int biome) {
    static const char *const n[BIOME_COUNT] = {
        "farmland", "ocean", "mountains", "desert", "arctic", "tropical", "forest", "coast",
        "the Himalaya"
    };
    return (biome >= 0 && biome < BIOME_COUNT) ? n[biome] : "?";
}

/* ---------------------------------------------------------------------------
 * The height function: shaders/terrain_fn.glsl, in C.
 * ------------------------------------------------------------------------- */
static uint32_t thash(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = ((uint32_t)x * 0x27d4eb2du) ^ ((uint32_t)y * 0x165667b1u) ^ seed;
    h = (h ^ (h >> 15)) * 0x85ebca6bu;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    return h ^ (h >> 16);
}
static float thval(int32_t x, int32_t y, uint32_t s) {
    return (float)(thash(x, y, s) >> 8) * (2.f / 16777216.f) - 1.f;
}

typedef struct { float x, y; } v2;
static v2 V2(float x, float y) { v2 r = { x, y }; return r; }
static v2 trot(v2 p) { return V2(0.8f * p.x - 0.6f * p.y, 0.6f * p.x + 0.8f * p.y); }

/* value, d/dx, d/dy */
static void tnoised(v2 p, uint32_t s, float out[3]) {
    float ix_f = floorf(p.x), iy_f = floorf(p.y);
    float fx = p.x - ix_f, fy = p.y - iy_f;
    float ux = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f);
    float uy = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    float dux = 30.f * fx * fx * (fx * (fx - 2.f) + 1.f);
    float duy = 30.f * fy * fy * (fy * (fy - 2.f) + 1.f);
    int32_t ix = (int32_t)ix_f, iy = (int32_t)iy_f;
    float a = thval(ix, iy, s), b = thval(ix + 1, iy, s);
    float c = thval(ix, iy + 1, s), d = thval(ix + 1, iy + 1, s);
    float k1 = b - a, k2 = c - a, k3 = a - b - c + d;
    out[0] = a + k1 * ux + k2 * uy + k3 * ux * uy;
    out[1] = dux * (k1 + k3 * uy);
    out[2] = duy * (k2 + k3 * ux);
}
static float tnoise(v2 p, uint32_t s) { float n[3]; tnoised(p, s, n); return n[0]; }

static float tfbm(v2 p, int oct, uint32_t s) {
    float sum = 0.f, amp = 0.5f;
    for (int i = 0; i < oct; ++i) {
        sum += amp * tnoise(p, s + (uint32_t)i * 7919u);
        p = trot(p);
        p.x *= 2.03f; p.y *= 2.03f;
        amp *= 0.5f;
    }
    return sum;
}
static float toct(float wave, float min_wave) { return clampf(wave / min_wave - 1.f, 0.f, 1.f); }

static float tfbm_lim(v2 p, float scale_m, int oct, uint32_t s, float min_wave) {
    float sum = 0.f, amp = 0.5f, wave = scale_m;
    for (int i = 0; i < oct; ++i) {
        float w = toct(wave, min_wave);
        if (w <= 0.f) break;
        sum += amp * w * tnoise(p, s + (uint32_t)i * 7919u);
        p = trot(p);
        p.x *= 2.f; p.y *= 2.f;
        wave *= 0.5f;
        amp *= 0.5f;
    }
    return sum;
}


static float tmountain(v2 p, float scale_m, uint32_t s, float min_wave) {
    float sum = 0.f, amp = 0.5f, wave = scale_m;
    float dx = 0.f, dy = 0.f;
    for (int i = 0; i < 11; ++i) {
        float w = toct(wave, min_wave);
        if (w <= 0.f) break;
        float n[3];
        tnoised(p, s + (uint32_t)i * 104729u, n);
        float r = 1.f - fabsf(n[0]);
        /* the fold's slope, its sign eased across the crest (see
         * terrain_fn.glsl: a hard flip makes a step along every crest) */
        float sg = -n[0] / (fabsf(n[0]) + 0.08f);
        dx += sg * n[1] * amp;
        dy += sg * n[2] * amp;
        sum += w * amp * r * r / (1.f + (dx * dx + dy * dy) * 1.4f);
        p = trot(p);
        p.x *= 2.f; p.y *= 2.f;
        wave *= 0.5f;
        amp *= 0.5f;
    }
    return sum;
}

static float height_one(const TerrainParams *t, double x, double z, float min_wave);

float terrain_height(const TerrainParams *t, double x, double z, float min_wave) {
    float h = height_one(t, x, z, min_wave);
    if (t->to && t->biome_mix > 0.f)
        h = lerpf(h, height_one(t->to, x, z, min_wave), t->biome_mix);
    return h;
}

static float height_one(const TerrainParams *t, double x, double z, float min_wave) {
    uint32_t seed = t->seed;
    float h = t->base;
    double cscale = t->cont_scale_km * 1000.0;
    h += t->cont_amp * tfbm_lim(V2((float)(x / cscale), (float)(z / cscale)), (float)cscale, 6, seed + 11u, min_wave);
    if (t->mtn_amp > 0.f) {
        double mscale = t->mtn_scale_km * 1000.0;
        v2 mp = V2((float)(x / (mscale * 5.0)) + 17.3f, (float)(z / (mscale * 5.0)) + 17.3f);
        float mask = smoothstepf(t->mtn_mask_lo, t->mtn_mask_hi, tfbm(mp, 3, seed + 23u));
        if (mask > 0.f)
            h += t->mtn_amp * mask * tmountain(V2((float)(x / mscale), (float)(z / mscale)),
                                               (float)mscale, seed + 31u, min_wave);
    }
    double hscale = t->hill_scale_km * 1000.0;
    h += t->hill_amp * tfbm_lim(V2((float)(x / hscale) + 5.1f, (float)(z / hscale) + 5.1f),
                                (float)hscale, 7, seed + 41u, min_wave);
    if (t->dune_amp > 0.f && min_wave < 900.f) {
        v2 q = V2((float)(x / 700.0), (float)(z / 700.0));
        v2 q2 = V2(q.x * 0.21f, q.y * 0.21f);
        q.x += 0.9f * tnoise(q2, seed + 51u);
        q.y += 0.9f * tnoise(V2(q2.x + 7.7f, q2.y + 7.7f), seed + 52u);
        float crest = fabsf(tnoise(V2(q.x, q.y * 0.33f), seed + 53u));
        float c1 = 1.f - crest;
        float dune = c1 * c1 * c1 * toct(700.f, min_wave);
        h += t->dune_amp * dune *
             smoothstepf(-0.2f, 0.4f, tnoise(V2((float)(x / 9000.0), (float)(z / 9000.0)), seed + 54u));
    }
    if (t->river > 0.f) {
        double rscale = t->river_scale_km * 1000.0;
        v2 q = V2((float)(x / rscale), (float)(z / rscale));
        float wx = tfbm(V2(q.x * 1.7f + 3.1f, q.y * 1.7f + 3.1f), 3, seed + 61u);
        float wz = tfbm(V2(q.x * 1.7f + 9.2f, q.y * 1.7f + 9.2f), 3, seed + 62u);
        q.x += 0.55f * wx;
        q.y += 0.55f * wz;
        float r = fabsf(tnoise(q, seed + 63u));
        float width = 0.012f * t->river;
        float bank = smoothstepf(width, width * 5.f, r);
        float low = smoothstepf(900.f, 300.f, h);
        h = lerpf(h, lerpf(-6.f, h, bank), low);
    }
    return h;
}

float terrain_max_near(const TerrainParams *t, double x, double z, float radius) {
    float m = terrain_height(t, x, z, 150.f);
    for (int ring = 1; ring <= 2; ++ring) {
        double r = radius * ring * 0.5;
        for (int k = 0; k < 8; ++k) {
            double a = k * (MR_PI_D / 4.0) + ring * 0.3;
            float h = terrain_height(t, x + r * cos(a), z + r * sin(a), 150.f);
            if (h > m) m = h;
        }
    }
    return m;
}

/* ---------------------------------------------------------------------------
 * The grid levels.
 * ------------------------------------------------------------------------- */
void terrain_grid_layout(TerrainGrid *g, double cam_x, double cam_z, float agl, float reach) {
    /* The finest spacing that still earns its triangles: about a hundredth of
     * the height above the ground, as a power of two, never under 4 m. */
    float s0 = 4.f;
    while (s0 * 110.f < agl && s0 < 512.f) s0 *= 2.f;
    g->levels = 0;
    float s = s0;
    while (g->levels < TERRAIN_MAX_LEVELS) {
        TerrainLevel *L = &g->level[g->levels++];
        L->spacing = s;
        /* Snapped to twice the spacing, so the vertex pattern is the one the
         * next level out would have, and the finer level always sits on
         * whole cells of the coarser one. */
        double step = 2.0 * s;
        double cx = floor(cam_x / step) * step;
        double cz = floor(cam_z / step) * step;
        L->origin_x = cx - (TERRAIN_GRID / 2) * (double)s;
        L->origin_z = cz - (TERRAIN_GRID / 2) * (double)s;
        if ((TERRAIN_GRID / 2) * s >= reach) break;
        s *= 2.f;
    }
}
