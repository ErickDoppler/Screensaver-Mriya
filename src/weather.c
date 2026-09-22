#include "weather.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

/* Passage timings, seconds: into the cloud, and back out of it. */
#define PASSAGE_IN        4.0f
#define PASSAGE_OUT       6.0f
#define PASSAGE_IN_QUICK  1.0f
#define PASSAGE_OUT_QUICK 2.2f
#define MORPH_TIME        60.0f   /* seconds a change of scenery takes */
#define MORPH_QUICK       12.0f   /* when the user asks for one */

static CloudLayer layer(float base, float top, float cover, float type,
                        float density, float scale_km, float erosion) {
    CloudLayer c = { base, top, cover, type, density, scale_km, erosion };
    return c;
}

static void defaults(Weather *w, rng_t *r) {
    memset(w, 0, sizeof *w);
    w->sun_elev = 35.f;
    w->sun_azim = rng_range(r, 0.f, 360.f);
    w->moon_elev = -20.f;
    w->moon_azim = rng_range(r, 0.f, 360.f);
    w->moon_phase = 0.5f;
    w->haze = 1.f;
    w->haze_height = 1200.f;
    w->humidity = 0.3f;
    w->low = layer(1500.f, 2500.f, 0.f, 0.5f, 1.f, 2.f, 0.5f);
    w->mid = layer(5500.f, 6200.f, 0.f, 0.3f, 1.f, 1.5f, 0.6f);
    w->cirrus_alt = 10500.f;
    w->wind_dir = rng_range(r, 0.f, 360.f);
    w->wind_speed = rng_range(r, 6.f, 22.f);
    w->biome = BIOME_FARMLAND;
    w->stars = 1.f;
    w->sea_state = 0.3f;
    w->alt_lo = 1000.f;
    w->alt_hi = 11000.f;
}

static int pick(rng_t *r, const int *choices, int n) { return choices[rng_int(r, n)]; }

void weather_make(Weather *w, int kind, rng_t *r) {
    defaults(w, r);
    static const int green[] = { BIOME_FARMLAND, BIOME_FARMLAND, BIOME_FOREST, BIOME_COAST };
    static const int lowland[] = { BIOME_FARMLAND, BIOME_COAST, BIOME_OCEAN, BIOME_FOREST };
    switch (kind) {
    case WX_CLEAR:
        w->sun_elev = rng_range(r, 22.f, 40.f);
        w->low = layer(1400.f, 2200.f, 0.16f, 0.55f, 1.f, 1.4f, 0.55f);
        w->cirrus_cover = 0.12f;
        w->haze = 1.1f;
        w->biome = pick(r, green, 4);
        break;
    case WX_CUMULUS:
        w->sun_elev = rng_range(r, 45.f, 62.f);
        w->low = layer(1300.f, 2900.f, 0.42f, 0.62f, 1.f, 2.0f, 0.55f);
        w->cirrus_cover = 0.2f;
        w->haze = 1.3f;
        w->humidity = 0.45f;
        w->biome = pick(r, green, 4);
        w->alt_lo = 1800.f; w->alt_hi = 4200.f;
        break;
    case WX_TOWERING:
        w->sun_elev = rng_range(r, 35.f, 55.f);
        w->low = layer(1200.f, 7800.f, 0.36f, 0.82f, 1.15f, 4.5f, 0.5f);
        w->turbulence = 0.35f;
        w->haze = 1.5f;
        w->humidity = 0.55f;
        w->biome = pick(r, green, 4);
        w->alt_lo = 3000.f; w->alt_hi = 6000.f;
        break;
    case WX_THUNDERSTORM:
        w->sun_elev = rng_range(r, 25.f, 45.f);
        w->low = layer(900.f, 11500.f, 0.58f, 1.0f, 1.5f, 7.0f, 0.45f);
        w->mid = layer(5200.f, 6600.f, 0.35f, 0.1f, 0.8f, 5.0f, 0.4f);
        w->rain = 0.8f;
        w->lightning = 9.f;
        w->turbulence = 0.8f;
        w->wind_speed = rng_range(r, 14.f, 26.f);
        w->haze = 2.2f;
        w->humidity = 0.85f;
        w->wetness = 1.f;
        w->biome = pick(r, green, 4);
        w->alt_lo = 3000.f; w->alt_hi = 7000.f;
        w->exposure = 0.3f;
        break;
    case WX_HEAVY_RAIN:
        w->sun_elev = rng_range(r, 20.f, 50.f);
        w->low = layer(550.f, 5500.f, 0.97f, 0.15f, 1.0f, 6.0f, 0.3f);
        w->rain = 1.f;
        w->turbulence = 0.35f;
        w->haze = 3.f;
        w->humidity = 1.f;
        w->wetness = 1.f;
        w->biome = pick(r, lowland, 4);
        w->alt_lo = 1400.f; w->alt_hi = 3500.f;
        w->exposure = 0.4f;
        break;
    case WX_CLOUD_SEA:
        w->sun_elev = rng_range(r, 25.f, 50.f);
        w->low = layer(1000.f, 2700.f, 0.93f, 0.32f, 1.f, 2.6f, 0.55f);
        w->cirrus_cover = 0.18f;
        w->cirrus_alt = 12000.f;
        w->biome = pick(r, lowland, 4);
        w->alt_lo = 5500.f; w->alt_hi = 10500.f;
        break;
    case WX_SUNSET:
        w->sun_elev = rng_range(r, 1.5f, 5.f);
        w->sun_azim = rng_range(r, 245.f, 295.f);
        w->low = layer(1500.f, 3100.f, 0.33f, 0.55f, 1.f, 2.2f, 0.55f);
        w->mid = layer(5500.f, 6300.f, 0.3f, 0.25f, 0.8f, 1.5f, 0.7f);
        w->cirrus_cover = 0.35f;
        w->haze = 1.8f;
        w->humidity = 0.5f;
        w->biome = pick(r, lowland, 4);
        break;
    case WX_SUNRISE:
        w->sun_elev = rng_range(r, 1.f, 4.f);
        w->sun_azim = rng_range(r, 70.f, 110.f);
        w->low = layer(350.f, 1300.f, 0.72f, 0.05f, 0.9f, 4.0f, 0.4f);
        w->haze = 1.5f;
        w->humidity = 0.6f;
        w->biome = BIOME_MOUNTAINS;
        w->alt_lo = 5000.f; w->alt_hi = 7500.f;
        break;
    case WX_MOONLIGHT:
        w->sun_elev = -30.f;
        w->moon_elev = rng_range(r, 25.f, 45.f);
        w->moon_phase = rng_range(r, 0.8f, 1.f);
        w->low = layer(1500.f, 2400.f, 0.14f, 0.5f, 1.f, 1.8f, 0.55f);
        w->city_lights = 1.f;
        w->biome = pick(r, lowland, 4) == BIOME_OCEAN ? BIOME_COAST : BIOME_FARMLAND;
        w->exposure = 0.6f;
        break;
    case WX_NIGHT_STORM:
        w->sun_elev = -35.f;
        w->moon_elev = rng_range(r, 10.f, 30.f);
        w->moon_phase = 0.4f;
        w->low = layer(900.f, 11000.f, 0.55f, 1.f, 1.5f, 7.0f, 0.45f);
        w->rain = 0.7f;
        w->lightning = 14.f;
        w->turbulence = 0.7f;
        w->haze = 2.f;
        w->humidity = 0.9f;
        w->wetness = 1.f;
        w->city_lights = 0.7f;
        w->biome = BIOME_FARMLAND;
        w->alt_lo = 3000.f; w->alt_hi = 6500.f;
        w->exposure = 0.8f;
        break;
    case WX_NIGHT_DECK:
        w->sun_elev = -40.f;
        w->moon_elev = rng_range(r, 25.f, 45.f);
        w->moon_phase = 1.f;
        w->low = layer(1100.f, 2800.f, 0.95f, 0.3f, 1.f, 2.5f, 0.55f);
        w->city_lights = 0.8f;
        w->alt_lo = 5000.f; w->alt_hi = 10500.f;
        w->exposure = 0.6f;
        break;
    case WX_CIRRUS:
        w->sun_elev = rng_range(r, 40.f, 60.f);
        w->cirrus_cover = 0.85f;
        w->cirrus_alt = 10000.f;
        w->haze = 1.3f;
        w->dust = 0.1f;
        w->biome = BIOME_DESERT;
        w->alt_lo = 2500.f; w->alt_hi = 8000.f;
        break;
    case WX_SNOWFALL:
        w->sun_elev = rng_range(r, 12.f, 22.f);
        w->low = layer(650.f, 3600.f, 0.86f, 0.2f, 0.9f, 4.0f, 0.4f);
        w->snow = 0.85f;
        w->snow_cover = 1.f;
        w->haze = 2.2f;
        w->humidity = 1.f;
        w->turbulence = 0.2f;
        w->biome = rng_f(r) < 0.5f ? BIOME_FOREST : BIOME_FARMLAND;
        w->alt_lo = 1200.f; w->alt_hi = 3000.f;
        w->exposure = 0.2f;
        break;
    case WX_HAZE:
        w->sun_elev = rng_range(r, 50.f, 68.f);
        w->haze = 5.f;
        w->haze_height = 2400.f;
        w->humidity = 0.7f;
        w->low = layer(1900.f, 2700.f, 0.14f, 0.55f, 1.f, 1.6f, 0.6f);
        w->biome = BIOME_FARMLAND;
        break;
    case WX_BLUE_HOUR:
        w->sun_elev = rng_range(r, -6.f, -3.5f);
        w->sun_azim = rng_range(r, 240.f, 300.f);
        w->moon_elev = rng_range(r, 10.f, 25.f);
        w->moon_azim = w->sun_azim - 150.f;
        w->moon_phase = 0.6f;
        w->low = layer(1500.f, 2800.f, 0.25f, 0.5f, 1.f, 2.0f, 0.55f);
        w->city_lights = 0.9f;
        w->haze = 1.4f;
        w->biome = pick(r, lowland, 4) == BIOME_OCEAN ? BIOME_COAST : BIOME_FARMLAND;
        w->exposure = 0.6f;
        break;
    case WX_DUST:
        w->sun_elev = rng_range(r, 18.f, 35.f);
        w->dust = 1.f;
        w->haze = 6.f;
        w->haze_height = 3000.f;
        w->humidity = 0.f;
        w->turbulence = 0.3f;
        w->wind_speed = rng_range(r, 16.f, 26.f);
        w->biome = BIOME_DESERT;
        w->alt_lo = 1500.f; w->alt_hi = 4000.f;
        break;
    case WX_AURORA:
        w->sun_elev = -25.f;
        w->moon_elev = rng_range(r, 3.f, 12.f);
        w->moon_phase = 0.2f;
        w->aurora = 1.f;
        w->low = layer(800.f, 1500.f, 0.12f, 0.3f, 0.8f, 3.0f, 0.5f);
        w->haze = 0.7f;
        w->biome = BIOME_ARCTIC;
        w->city_lights = 0.05f;
        w->exposure = 1.0f;
        break;
    case WX_STRATOCUMULUS:
        w->sun_elev = rng_range(r, 20.f, 45.f);
        w->low = layer(1100.f, 1950.f, 0.74f, 0.3f, 1.f, 1.8f, 0.6f);
        w->haze = 1.6f;
        w->humidity = 0.7f;
        w->biome = pick(r, green, 4);
        break;
    case WX_ALTOCUMULUS:
        w->sun_elev = rng_range(r, 30.f, 55.f);
        w->mid = layer(5000.f, 5600.f, 0.58f, 0.38f, 0.8f, 0.8f, 0.8f);
        w->low = layer(1400.f, 2100.f, 0.08f, 0.55f, 1.f, 1.3f, 0.6f);
        w->biome = pick(r, green, 4);
        w->alt_lo = 1500.f; w->alt_hi = 4000.f;
        break;
    case WX_TROPICAL:
        w->sun_elev = rng_range(r, 55.f, 75.f);
        w->low = layer(700.f, 4800.f, 0.34f, 0.72f, 1.1f, 3.0f, 0.5f);
        w->humidity = 0.8f;
        w->haze = 1.6f;
        w->sea_state = 0.35f;
        w->biome = BIOME_TROPICAL;
        w->alt_lo = 1500.f; w->alt_hi = 5500.f;
        break;
    case WX_BETWEEN_LAYERS:
        w->sun_elev = rng_range(r, 30.f, 50.f);
        w->low = layer(1000.f, 2300.f, 0.9f, 0.3f, 1.f, 2.4f, 0.55f);
        w->mid = layer(6200.f, 7200.f, 0.9f, 0.18f, 0.9f, 3.0f, 0.5f);
        w->haze = 1.4f;
        w->humidity = 0.6f;
        w->alt_lo = 3500.f; w->alt_hi = 5200.f;
        break;
    case WX_ALPINE:
        w->sun_elev = rng_range(r, 30.f, 50.f);
        w->low = layer(3900.f, 5300.f, 0.18f, 0.6f, 1.f, 2.0f, 0.55f);
        w->haze = 0.8f;
        w->humidity = 0.2f;
        w->biome = BIOME_MOUNTAINS;
        w->alt_lo = 5500.f; w->alt_hi = 8500.f;
        break;
    case WX_HIMALAYA:
        /* high, dry, dark blue air; cumulus building over the plateau below
         * the peaks, cirrus streaming off the summits in the jet */
        w->sun_elev = rng_range(r, 25.f, 50.f);
        w->low = layer(5600.f, 7000.f, 0.2f, 0.55f, 1.f, 1.8f, 0.55f);
        w->cirrus_cover = 0.2f;
        w->haze = 0.55f;
        w->haze_height = 900.f;
        w->humidity = 0.2f;
        w->wind_speed = rng_range(r, 20.f, 40.f);
        w->turbulence = 0.25f;
        w->biome = BIOME_HIMALAYA;
        w->alt_lo = 9000.f; w->alt_hi = 11000.f;
        break;
    case WX_ARCTIC_DAY:
        w->sun_elev = rng_range(r, 5.f, 12.f);
        w->low = layer(300.f, 750.f, 0.2f, 0.1f, 0.8f, 3.0f, 0.5f);
        w->haze = 0.7f;
        w->humidity = 0.3f;
        w->biome = BIOME_ARCTIC;
        break;
    case WX_DRIZZLE:
        w->sun_elev = rng_range(r, 20.f, 40.f);
        w->low = layer(520.f, 1900.f, 0.95f, 0.1f, 0.9f, 4.0f, 0.4f);
        w->rain = 0.3f;
        w->haze = 2.6f;
        w->humidity = 1.f;
        w->wetness = 0.8f;
        w->biome = pick(r, green, 4);
        w->alt_lo = 1000.f; w->alt_hi = 1500.f;
        w->exposure = 0.3f;
        break;
    case WX_GOLDEN_SEA:
        w->sun_elev = rng_range(r, 8.f, 16.f);
        w->low = layer(1200.f, 2300.f, 0.2f, 0.55f, 1.f, 1.8f, 0.55f);
        w->sea_state = 0.45f;
        w->haze = 1.4f;
        w->humidity = 0.5f;
        w->biome = BIOME_OCEAN;
        break;
    case WX_STORMY_SUNSET:
        w->sun_elev = rng_range(r, 2.f, 6.f);
        w->sun_azim = rng_range(r, 245.f, 295.f);
        w->low = layer(1000.f, 10000.f, 0.45f, 0.95f, 1.4f, 6.0f, 0.45f);
        w->mid = layer(5500.f, 6500.f, 0.2f, 0.2f, 0.8f, 2.0f, 0.6f);
        w->rain = 0.4f;
        w->lightning = 3.f;
        w->turbulence = 0.4f;
        w->haze = 1.6f;
        w->humidity = 0.7f;
        w->biome = pick(r, green, 4);
        w->alt_lo = 2500.f; w->alt_hi = 6000.f;
        break;
    default:
        break;
    }
}

static vec3 dir_from(float elev_deg, float azim_deg) {
    float e = DEG2RAD(elev_deg), a = DEG2RAD(azim_deg);
    /* azimuth from north (-z) clockwise towards east (+x) */
    return v3(cosf(e) * sinf(a), sinf(e), -cosf(e) * cosf(a));
}
vec3 weather_sun_dir(const Weather *w) { return dir_from(w->sun_elev, w->sun_azim); }
vec3 weather_moon_dir(const Weather *w) { return dir_from(w->moon_elev, w->moon_azim); }

/* --- the deck -------------------------------------------------------------- */
static void reshuffle(WeatherState *w, const Settings *s) {
    w->deck_n = 0;
    for (int i = 0; i < WX_COUNT; ++i)
        if (settings_scene_enabled(s, i)) w->deck[w->deck_n++] = i;
    if (w->deck_n == 0) w->deck[w->deck_n++] = WX_CLEAR;
    for (int i = w->deck_n - 1; i > 0; --i) {
        int j = rng_int(&w->rng, i + 1);
        int t = w->deck[i]; w->deck[i] = w->deck[j]; w->deck[j] = t;
    }
    /* A fresh deck never opens with the one just shown. */
    if (w->deck_n > 1 && w->deck[0] == w->last_kind) {
        int t = w->deck[0]; w->deck[0] = w->deck[1]; w->deck[1] = t;
    }
    w->deck_pos = 0;
    char order[768];
    int n = 0;
    for (int i = 0; i < w->deck_n && n < (int)sizeof order - 24; ++i)
        n += snprintf(order + n, sizeof order - (size_t)n, "%s%s", i ? " " : "",
                      settings_scene_name(w->deck[i]));
    plat_log("weather deck: %s", order);
}

static int deal(WeatherState *w, const Settings *s) {
    if (w->deck_pos >= w->deck_n) reshuffle(w, s);
    return w->deck[w->deck_pos++];
}

/* --- the ground ------------------------------------------------------------
 * One terrain seed for the whole run: a change of scenery reshapes the same
 * land - plains rising into ranges, the sea flooding in - rather than
 * swapping one map for another. */
static void ground_for(const WeatherState *w, const Weather *wx, TerrainParams *t) {
    terrain_params(t, wx->biome, w->world_seed);
    if (wx->snow_cover > t->snow_cover) t->snow_cover = wx->snow_cover;
}

static float lerpa(float a, float b, float t) {       /* degrees, the short way */
    float d = fmodf(b - a + 540.f, 360.f) - 180.f;
    return a + d * t;
}

static CloudLayer lerp_layer(CloudLayer a, CloudLayer b, float t) {
    /* a layer that is not there has no height: it grows where the other is */
    if (a.cover <= 0.f) { CloudLayer z = b; z.cover = 0.f; a = z; }
    if (b.cover <= 0.f) { CloudLayer z = a; z.cover = 0.f; b = z; }
    CloudLayer c;
    c.base = lerpf(a.base, b.base, t);
    c.top = lerpf(a.top, b.top, t);
    c.cover = lerpf(a.cover, b.cover, t);
    c.type = lerpf(a.type, b.type, t);
    c.density = lerpf(a.density, b.density, t);
    c.scale_km = lerpf(a.scale_km, b.scale_km, t);
    c.erosion = lerpf(a.erosion, b.erosion, t);
    return c;
}

static void lerp_weather(Weather *o, const Weather *a, const Weather *b, float t) {
    *o = t < 0.5f ? *a : *b;                 /* the discrete fields */
#define L(f) o->f = lerpf(a->f, b->f, t)
    L(sun_elev); L(moon_elev); L(moon_phase);
    L(haze); L(haze_height); L(dust); L(humidity);
    L(cirrus_cover); L(cirrus_alt);
    L(rain); L(snow); L(lightning); L(turbulence); L(wind_speed);
    L(snow_cover); L(wetness); L(city_lights); L(sea_state);
    L(aurora); L(stars); L(exposure); L(alt_lo); L(alt_hi);
#undef L
    o->sun_azim = lerpa(a->sun_azim, b->sun_azim, t);
    o->moon_azim = lerpa(a->moon_azim, b->moon_azim, t);
    o->wind_dir = lerpa(a->wind_dir, b->wind_dir, t);
    o->low = lerp_layer(a->low, b->low, t);
    o->mid = lerp_layer(a->mid, b->mid, t);
}

/* The ground part way through a change: the old ground, blended towards the
 * new by height and by land cover (terrain.c does the blending). */
static void blend_terrain(TerrainParams *o, const TerrainParams *a, const TerrainParams *b, float t) {
    *o = *a;
    o->to = b;
    o->biome2 = b->biome;
    o->biome_mix = t;
}

void weather_set(WeatherState *w, int kind) {
    w->kind = kind;
    w->last_kind = kind;
    weather_make(&w->cur, kind, &w->rng);
    w->from = w->next = w->cur;
    w->next_kind = kind;
    w->passage = 0.f;
    w->passage_dir = 0;
    w->since_change = 0.f;
    w->wblend = w->tblend = 1.f;
    ground_for(w, &w->cur, &w->terrain);
    w->t_from = w->t_to = w->terrain;
    w->terrain_changed = 1;
    plat_log("weather: %s over %s", settings_scene_name(kind), terrain_biome_name(w->cur.biome));
}

void weather_init(WeatherState *w, const Settings *s, unsigned seed, int first) {
    memset(w, 0, sizeof *w);
    w->rng.s = seed ? seed : 0x1234567u;
    w->world_seed = rng_u32(&w->rng);
    w->last_kind = -1;
    reshuffle(w, s);
    int kind = first >= 0 ? first : deal(w, s);
    if (first >= 0) {
        /* the one asked for counts as dealt */
        for (int i = w->deck_pos; i < w->deck_n; ++i)
            if (w->deck[i] == first) {
                int t = w->deck[w->deck_pos]; w->deck[w->deck_pos] = w->deck[i]; w->deck[i] = t;
                w->deck_pos++;
                break;
            }
    }
    weather_set(w, kind);
    w->flash_timer = 5.f;
    w->flash_age = 99.f;
}

static float passage_rate(int dir) {
    return dir > 0 ? 1.f / PASSAGE_IN_QUICK : 1.f / PASSAGE_OUT_QUICK;
}

/* The highest the ground would stand near the aircraft under these terrain
 * parameters - what it must stay clear of. */
static float peak_near(const TerrainParams *t, dvec3 ac) {
    return fmaxf(terrain_max_near(t, ac.x, ac.z, 8000.f), 0.f);
}

void weather_next(WeatherState *w, const Settings *s, int quick) {
    if (w->passage_dir > 0) return;               /* already inside a jump */
    w->next_kind = deal(w, s);
    /* Whatever is showing - even halfway through a change - is where the
     * next change starts from. */
    w->from = w->cur;
    /* the ground it starts from: whichever of the two it is nearer, if a
     * change was still going on */
    w->t_from = w->terrain.to && w->terrain.biome_mix > 0.5f ? *w->terrain.to : w->terrain;
    w->t_from.to = NULL;
    w->t_from.biome_mix = 0.f;
    w->t_from.biome2 = w->t_from.biome;
    weather_make(&w->next, w->next_kind, &w->rng);
    ground_for(w, &w->next, &w->t_to);
    w->kind = w->next_kind;
    w->last_kind = w->kind;
    w->wblend = w->tblend = 0.f;
    w->morph_time = quick ? MORPH_QUICK : MORPH_TIME;
    w->quick = quick;
    w->since_change = 0.f;
    w->dealt = 1;
    plat_log("weather: turning to %s over %s", settings_scene_name(w->next_kind),
             terrain_biome_name(w->next.biome));
}

void weather_to(WeatherState *w, const Weather *target, int quick) {
    w->from = w->cur;
    w->t_from = w->terrain.to && w->terrain.biome_mix > 0.5f ? *w->terrain.to : w->terrain;
    w->t_from.to = NULL;
    w->t_from.biome_mix = 0.f;
    w->t_from.biome2 = w->t_from.biome;
    w->next = *target;
    ground_for(w, &w->next, &w->t_to);
    w->wblend = w->tblend = 0.f;
    w->morph_time = quick ? MORPH_QUICK : MORPH_TIME;
    w->quick = quick;
    w->since_change = 0.f;
    w->dealt = 1;
}

int weather_update(WeatherState *w, const Settings *s, float dt, dvec3 ac) {
    w->terrain_changed = 0;
    w->time += dt;

    /* The air mass drifts with the wind; clouds are anchored to it. */
    float wd = DEG2RAD(w->cur.wind_dir);
    /* the wind comes FROM wind_dir, so the air moves the other way */
    w->wind_offset_x += -sin(wd) * w->cur.wind_speed * dt;
    w->wind_offset_z += cos(wd) * w->cur.wind_speed * dt;

    /* the hold, then the next change */
    int changing = w->wblend < 1.f || w->tblend < 1.f || w->passage_dir != 0;
    if (!changing) w->since_change += dt;
    if (!changing && s->weather_minutes > 0 && w->since_change > s->weather_minutes * 60.f)
        weather_next(w, s, 0);
    int dealt = w->dealt;
    w->dealt = 0;

    /* A quick change into much higher ground, with the aircraft low, cannot
     * grow mountains under it in a few seconds: that one is hidden in a cloud
     * bank instead, as a jump (the app moves the aircraft up inside). */
    if (dealt && w->quick && w->passage_dir == 0 &&
        peak_near(&w->t_to, ac) + 700.f > (float)ac.y && peak_near(&w->t_to, ac) > peak_near(&w->t_from, ac) + 500.f) {
        w->passage_dir = 1;
        plat_log("weather: a jump - the new ground is too high to grow under the aircraft");
    }

    if (w->passage_dir > 0) {
        w->passage += dt * passage_rate(1);
        if (w->passage >= 1.f) {
            w->passage = 1.f;
            w->wblend = w->tblend = 1.f;
            w->cur = w->next;
            w->terrain = w->t_to;
            w->terrain_changed = 1;
            w->passage_dir = -1;
            dealt |= 2;                             /* place the aircraft now */
        }
    } else if (w->passage_dir < 0) {
        w->passage -= dt * passage_rate(-1);
        if (w->passage <= 0.f) { w->passage = 0.f; w->passage_dir = 0; }
    }

    if (w->passage_dir == 0 && (w->wblend < 1.f || w->tblend < 1.f)) {
        /* the sky and the air: a steady change over the morph time */
        w->wblend = fminf(1.f, w->wblend + dt / w->morph_time);
        /* The ground at the same pace - but never rising to within 600 m of
         * the aircraft: where it would, it waits for the aircraft to climb. */
        float rate = dt / w->morph_time;
        if (w->tblend < 1.f) {
            TerrainParams ahead;
            float tb2 = fminf(1.f, w->tblend + 0.08f);          /* a few seconds on */
            blend_terrain(&ahead, &w->t_from, &w->t_to, smoothstepf(0.f, 1.f, tb2));
            float room = (float)ac.y - (peak_near(&ahead, ac) + 600.f);
            if (room < 400.f && peak_near(&w->t_to, ac) > peak_near(&w->t_from, ac))
                rate *= clampf(room / 400.f, 0.f, 1.f);
            w->tblend = fminf(1.f, w->tblend + rate);
        }
        lerp_weather(&w->cur, &w->from, &w->next, smoothstepf(0.f, 1.f, w->wblend));
        blend_terrain(&w->terrain, &w->t_from, &w->t_to, smoothstepf(0.f, 1.f, w->tblend));
        w->terrain_changed = 1;
        if (w->wblend >= 1.f && w->tblend >= 1.f) {
            w->cur = w->next;
            w->terrain = w->t_to;
            plat_log("weather: now %s over %s", settings_scene_name(w->kind),
                     terrain_biome_name(w->cur.biome));
        }
    }

    /* Lightning: a Poisson process at the scenario's rate. Each flash is a
     * few return strokes in quick succession, which is what makes real
     * lightning flicker rather than blink. */
    w->flash_age += dt;
    if (w->cur.lightning > 0.f && w->passage < 0.5f) {
        w->flash_timer -= dt;
        if (w->flash_timer <= 0.f) {
            float rate = w->cur.lightning / 60.f;
            w->flash_timer = -logf(fmaxf(rng_f(&w->rng), 1e-4f)) / rate;
            float ang = rng_range(&w->rng, 0.f, 2.f * MR_PI);
            float dist = rng_range(&w->rng, 2500.f, 22000.f);
            float lo = w->cur.low.base, hi = fminf(w->cur.low.top, 9000.f);
            w->flash_pos = dv3(ac.x + cosf(ang) * dist,
                               rng_range(&w->rng, lo + 300.f, lo + (hi - lo) * 0.6f),
                               ac.z + sinf(ang) * dist);
            w->bolt_visible = rng_f(&w->rng) < 0.45f;
            w->bolt_seed = rng_u32(&w->rng);
            w->flash_age = 0.f;
        }
    }
    /* strokes at 0, ~0.08, ~0.19, ~0.33 s, each decaying fast */
    {
        float a = w->flash_age, f = 0.f;
        static const float strokes[4] = { 0.f, 0.075f, 0.19f, 0.33f };
        static const float power[4] = { 1.f, 0.55f, 0.8f, 0.35f };
        for (int k = 0; k < 4; ++k) {
            float t = a - strokes[k];
            if (t >= 0.f) f += power[k] * expf(-t / 0.045f);
        }
        w->flash = a < 1.5f ? f : 0.f;
    }
    return dealt;
}
