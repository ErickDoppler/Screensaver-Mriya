/* The weather scenarios: what the sky, the clouds, the air and the ground are
 * doing. Each scenario is one complete Weather; the rotation deals them from
 * a shuffled deck so none repeats until every enabled one has been seen. The
 * change from one to the next is gradual: over a minute the sky and the air
 * turn into the next scenario and the ground reshapes itself under the
 * aircraft, then it holds. Only a quick change (the user's) into ground much
 * higher than the aircraft is hidden in a cloud bank instead (the "passage"),
 * where the aircraft is moved up. */
#ifndef MR_WEATHER_H
#define MR_WEATHER_H
#include "mathx.h"
#include "settings.h"
#include "terrain.h"

/* One cloud layer. Heights in metres above sea level. */
typedef struct CloudLayer {
    float base, top;
    float cover;          /* 0..1: how much of the sky the layer fills */
    float type;           /* 0 stratus (flat), 0.5 cumulus (heaped), 1 cumulonimbus */
    float density;        /* extinction multiplier */
    float scale_km;       /* horizontal size of the cloud features */
    float erosion;        /* how much the detail noise eats the edges (0..1) */
} CloudLayer;

typedef struct Weather {
    /* light */
    float sun_elev, sun_azim;         /* degrees; azimuth from north, clockwise */
    float moon_elev, moon_azim;
    float moon_phase;                 /* 0 new .. 1 full */
    /* the air */
    float haze;                       /* aerosol density multiplier, 1 = clean */
    float haze_height;                /* aerosol scale height, metres */
    float dust;                       /* 0..1: aerosol turns ochre and absorbs blue */
    float humidity;                   /* 0..1: whitens the haze */
    /* clouds */
    CloudLayer low, mid;
    float cirrus_cover, cirrus_alt;
    /* what falls out of them */
    float rain;                       /* 0..1 intensity */
    float snow;                       /* 0..1 intensity */
    float lightning;                  /* flashes per minute */
    float turbulence;                 /* 0..1 */
    float wind_dir, wind_speed;       /* degrees the wind comes FROM; m/s at altitude */
    /* the ground */
    int   biome;
    float snow_cover;                 /* 0..1 on top of the biome's own */
    float wetness;
    float city_lights;                /* 0..1 */
    float sea_state;                  /* 0 glassy .. 1 gale */
    /* night extras */
    float aurora;
    float stars;                      /* 0..1 extra dimming of the star field */
    float exposure;                   /* stops, added to the automatic exposure */
    /* where the aircraft likes to be when this scenario opens */
    float alt_lo, alt_hi;
} Weather;

typedef struct WeatherState {
    int     kind;                     /* the scenario showing (or arriving) */
    Weather cur;                      /* what is being drawn */
    Weather from, next;               /* the change runs from one to the other */
    int     next_kind;
    float   wblend, tblend;           /* 0..1: how far the sky and the ground have come */
    float   morph_time;               /* seconds the change takes */
    int     quick;                    /* the user asked for this change */
    int     dealt;                    /* a change began (for weather_update's return) */
    TerrainParams t_from, t_to;       /* the ground the change runs between */
    unsigned world_seed;              /* one terrain for the whole run */
    /* the passage (a jump): 0 = clear, rises to 1 (inside the cloud) and falls back */
    float   passage;
    int     passage_dir;              /* +1 going in, -1 coming out, 0 none */
    int     swapped;                  /* 1 once the switch in the middle happened */
    float   since_change;             /* seconds the current scenario has shown */
    /* the deck */
    int     deck[WX_COUNT], deck_n, deck_pos, last_kind;
    rng_t   rng;
    /* ground */
    TerrainParams terrain;
    int     terrain_changed;          /* set when the biome was swapped this frame */
    float   seed_offset;
    /* lightning */
    float   flash;                    /* current flash brightness */
    dvec3   flash_pos;                /* where in the world it is */
    float   flash_timer;
    int     bolt_visible;             /* draw a channel to the ground this flash */
    unsigned bolt_seed;
    float   flash_age;
    /* aurora and cloud drift clocks */
    float   time;
    double  wind_offset_x, wind_offset_z;   /* how far the air mass has moved */
} WeatherState;

void weather_init(WeatherState *w, const Settings *s, unsigned seed, int first);
/* Deals the next scenario and starts the change into it. `quick` (the user
 * asked for it) makes the change take seconds rather than a minute. */
void weather_next(WeatherState *w, const Settings *s, int quick);
/* Starts a change into a given Weather (real time: the live conditions). */
void weather_to(WeatherState *w, const Weather *target, int quick);
/* Jumps straight to a scenario with no passage (start-up, command line). */
void weather_set(WeatherState *w, int kind);
/* Advances the change, the automatic rotation and the lightning. Returns a
 * bit mask: 1 when a new scenario was dealt this frame (the autopilot may
 * want another altitude), 2 when a jump swapped everything at once (the
 * aircraft may have to be moved). */
int  weather_update(WeatherState *w, const Settings *s, float dt, dvec3 aircraft_pos);
/* Fills a Weather for one scenario (with a little randomness). */
void weather_make(Weather *out, int kind, rng_t *rng);

/* Sun and moon directions in the world frame (x east, y up, z south). */
vec3 weather_sun_dir(const Weather *w);
vec3 weather_moon_dir(const Weather *w);
#endif
