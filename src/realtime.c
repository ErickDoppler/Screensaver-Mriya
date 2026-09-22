#include "realtime.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FETCH_EVERY_S (15 * 60)

/* --- the sky ------------------------------------------------------------------
 * Low-precision solar and lunar positions (the Astronomical Almanac's short
 * series): a tenth of a degree for the sun, half a degree for the moon. */
static double rad(double d) { return d * (3.14159265358979 / 180.0); }
static double deg(double r) { return r * (180.0 / 3.14159265358979); }
static double norm360(double a) { a = fmod(a, 360.0); return a < 0.0 ? a + 360.0 : a; }

/* equatorial (right ascension, declination; radians) to the local sky */
static void to_sky(double ra, double dec, double lat, double lst_deg, float *elev, float *azim) {
    double ha = rad(lst_deg) - ra;
    double la = rad(lat);
    double se = sin(la) * sin(dec) + cos(la) * cos(dec) * cos(ha);
    double e = asin(se < -1.0 ? -1.0 : se > 1.0 ? 1.0 : se);
    double az = atan2(-cos(dec) * sin(ha), sin(dec) * cos(la) - cos(dec) * cos(ha) * sin(la));
    *elev = (float)deg(e);
    *azim = (float)norm360(deg(az));
}

void realtime_sky(double lat, double lon, time_t utc, float *sun_elev, float *sun_azim,
                  float *moon_elev, float *moon_azim, float *moon_phase) {
    double n = (double)utc / 86400.0 + 2440587.5 - 2451545.0;     /* days since J2000 */
    double eps = rad(23.439 - 0.0000004 * n);
    double lst = norm360(280.46061837 + 360.98564736629 * n + lon);  /* local sidereal time */
    /* the sun */
    double L = norm360(280.460 + 0.9856474 * n), g = rad(norm360(357.528 + 0.9856003 * n));
    double lam = rad(L + 1.915 * sin(g) + 0.020 * sin(2.0 * g));
    double ra = atan2(cos(eps) * sin(lam), cos(lam)), dec = asin(sin(eps) * sin(lam));
    to_sky(ra, dec, lat, lst, sun_elev, sun_azim);
    /* the moon */
    double Lm = norm360(218.316 + 13.176396 * n);
    double Mm = rad(norm360(134.963 + 13.064993 * n));
    double Fm = rad(norm360(93.272 + 13.229350 * n));
    double lm = rad(Lm + 6.289 * sin(Mm)), bm = rad(5.128 * sin(Fm));
    double x = cos(bm) * cos(lm), y = cos(bm) * sin(lm), z = sin(bm);
    double ye = y * cos(eps) - z * sin(eps), ze = y * sin(eps) + z * cos(eps);
    to_sky(atan2(ye, x), asin(ze), lat, lst, moon_elev, moon_azim);
    /* lit fraction from the moon's elongation from the sun */
    double ce = cos(bm) * cos(lm - lam);
    *moon_phase = (float)((1.0 - ce) * 0.5);
}

/* --- a little JSON ------------------------------------------------------------
 * Enough to read Open-Meteo's flat objects: the number or string after a key,
 * searched from a given point. */
static int json_num(const char *from, const char *key, double *out) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(from, pat);
    if (!p) return 0;
    p += strlen(pat);
    char *end;
    double v = strtod(p, &end);
    if (end == p) return 0;
    *out = v;
    return 1;
}

static int json_str(const char *from, const char *key, char *out, int cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(from, pat);
    if (!p) return 0;
    p += strlen(pat);
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}

static void url_encode(const char *s, char *out, int cap) {
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *s && n < cap - 4; ++s) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}

int realtime_geocode(const char *query, char *name, int name_cap, double *lat, double *lon) {
    char q[400], path[600];
    url_encode(query, q, sizeof q);
    snprintf(path, sizeof path, "/v1/search?name=%s&count=1&language=en&format=json", q);
    char *buf = malloc(65536);
    if (!buf) return 0;
    int ok = 0;
    if (plat_https_get("geocoding-api.open-meteo.com", path, buf, 65536) > 0) {
        const char *r = strstr(buf, "\"results\":[");
        char nm[128] = "", country[128] = "";
        if (r && json_str(r, "name", nm, sizeof nm) && json_num(r, "latitude", lat) && json_num(r, "longitude", lon)) {
            json_str(r, "country", country, sizeof country);
            snprintf(name, (size_t)name_cap, "%s%s%s", nm, country[0] ? ", " : "", country);
            ok = 1;
        }
    }
    free(buf);
    return ok;
}

/* --- the conditions, as a scenario --------------------------------------------- */
typedef struct Live {
    double code, cloud, low, mid, high, precip, rain, snowfall, wind, wind_dir, vis, temp, elevation;
} Live;

static const char *code_text(int c) {
    if (c == 0) return "clear";
    if (c <= 2) return "partly cloudy";
    if (c == 3) return "overcast";
    if (c == 45 || c == 48) return "fog";
    if (c >= 51 && c <= 57) return "drizzle";
    if (c >= 61 && c <= 67) return "rain";
    if (c >= 71 && c <= 77) return "snow";
    if (c >= 80 && c <= 82) return "rain showers";
    if (c >= 85 && c <= 86) return "snow showers";
    if (c >= 95) return "thunderstorm";
    return "cloudy";
}

static void make_weather(const Live *l, double lat, double lon, Weather *w) {
    float se, sa, me, ma, mp;
    realtime_sky(lat, lon, time(NULL), &se, &sa, &me, &ma, &mp);
    int day = se > -4.f;
    int c = (int)l->code;
    int kind;
    if (c >= 95)                                   kind = day ? WX_THUNDERSTORM : WX_NIGHT_STORM;
    else if ((c >= 71 && c <= 77) || c == 85 || c == 86) kind = WX_SNOWFALL;
    else if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) kind = l->precip > 4.0 ? WX_HEAVY_RAIN : WX_DRIZZLE;
    else if (c >= 51 && c <= 57)                   kind = WX_DRIZZLE;
    else if (c == 45 || c == 48)                   kind = day ? WX_HAZE : WX_NIGHT_DECK;
    else if (c == 3)                               kind = day ? WX_STRATOCUMULUS : WX_NIGHT_DECK;
    else if (c == 2)                               kind = day ? WX_CUMULUS : WX_MOONLIGHT;
    else if (!day)                                 kind = fabs(lat) > 62.0 ? WX_AURORA : WX_MOONLIGHT;
    else                                           kind = se < 10.f ? WX_SUNSET : WX_CLEAR;
    rng_t r = { (unsigned)time(NULL) | 1u };
    weather_make(w, kind, &r);

    /* the sky as it really is */
    w->sun_elev = se; w->sun_azim = sa;
    w->moon_elev = me; w->moon_azim = ma; w->moon_phase = mp;
    /* the cloud layers, by how much of the sky each covers */
    float low = (float)(l->low / 100.0), mid = (float)(l->mid / 100.0), high = (float)(l->high / 100.0);
    if (low > 0.02f && w->low.top <= w->low.base) { w->low.base = 1200.f; w->low.top = 2400.f; }
    if (low > 0.02f && w->low.cover <= 0.f) { w->low.type = 0.4f; w->low.density = 1.f; w->low.scale_km = 2.f; w->low.erosion = 0.5f; }
    w->low.cover = low * 0.92f;
    if (mid > 0.02f && w->mid.cover <= 0.f) {
        w->mid.base = 4300.f; w->mid.top = 5300.f; w->mid.type = 0.2f;
        w->mid.density = 0.8f; w->mid.scale_km = 3.f; w->mid.erosion = 0.55f;
    }
    w->mid.cover = mid * 0.85f;
    w->cirrus_cover = high * 0.8f;
    /* what falls, and the wind aloft (a little over twice the surface wind) */
    w->rain = (float)fmin(l->rain / 6.0, 1.0);
    w->snow = (float)fmin(l->snowfall / 2.0, 1.0);
    if (w->snow > 0.f) w->rain = 0.f;
    w->wind_speed = (float)fmin(fmax(l->wind * 2.3, 4.0), 60.0);
    w->wind_dir = (float)l->wind_dir;
    /* the air: haze from the visibility */
    if (l->vis > 0.0) w->haze = l->vis < 2000.0 ? 3.5f : l->vis < 8000.0 ? 2.2f : l->vis < 20000.0 ? 1.4f : 1.0f;
    w->wetness = w->rain > 0.f ? 0.8f : 0.f;
    if (l->temp < -2.0 && w->snow_cover < 0.6f) w->snow_cover = 0.6f;
    /* the ground, from the place */
    double al = fabs(lat);
    if (l->elevation > 3000.0)      w->biome = BIOME_HIMALAYA;
    else if (l->elevation > 1200.0) w->biome = BIOME_MOUNTAINS;
    else if (al > 66.0)             w->biome = BIOME_ARCTIC;
    else if (al < 20.0)             w->biome = BIOME_TROPICAL;
    else if (al > 55.0)             w->biome = BIOME_FOREST;
    else                            w->biome = BIOME_FARMLAND;
    if (w->biome == BIOME_HIMALAYA) { w->alt_lo = 9000.f; w->alt_hi = 11000.f; }
}

/* --- the background fetch ------------------------------------------------------- */
static SDL_Thread *g_thread;
static SDL_Mutex *g_mutex;
static SDL_Condition *g_cond;
static int g_quit, g_now, g_fresh;
static double g_lat, g_lon;
static Weather g_weather;
static char g_desc[160];

static int fetch_once(void) {
    char path[512];
    snprintf(path, sizeof path,
             "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,precipitation,rain,snowfall,"
             "weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high,wind_speed_10m,"
             "wind_direction_10m,visibility&wind_speed_unit=ms&timezone=GMT", g_lat, g_lon);
    char *buf = malloc(32768);
    if (!buf) return 0;
    int ok = 0;
    if (plat_https_get("api.open-meteo.com", path, buf, 32768) > 0) {
        Live l;
        memset(&l, 0, sizeof l);
        l.vis = -1.0;
        json_num(buf, "elevation", &l.elevation);
        const char *c = strstr(buf, "\"current\":{");
        if (c && json_num(c, "weather_code", &l.code)) {
            json_num(c, "cloud_cover", &l.cloud);
            json_num(c, "cloud_cover_low", &l.low);
            json_num(c, "cloud_cover_mid", &l.mid);
            json_num(c, "cloud_cover_high", &l.high);
            json_num(c, "precipitation", &l.precip);
            json_num(c, "rain", &l.rain);
            json_num(c, "snowfall", &l.snowfall);
            json_num(c, "wind_speed_10m", &l.wind);
            json_num(c, "wind_direction_10m", &l.wind_dir);
            json_num(c, "visibility", &l.vis);
            json_num(c, "temperature_2m", &l.temp);
            Weather w;
            make_weather(&l, g_lat, g_lon, &w);
            SDL_LockMutex(g_mutex);
            g_weather = w;
            snprintf(g_desc, sizeof g_desc, "%s, %.0f\xC2\xB0""C, wind %.0f m/s", code_text((int)l.code), l.temp, l.wind);
            g_fresh = 1;
            SDL_UnlockMutex(g_mutex);
            plat_log("real time: %s (low %.0f%% mid %.0f%% high %.0f%%, vis %.0f m, elevation %.0f m)",
                     g_desc, l.low, l.mid, l.high, l.vis, l.elevation);
            ok = 1;
        }
    }
    if (!ok) plat_log("real time: no weather this time");
    free(buf);
    return ok;
}

static int SDLCALL fetch_thread(void *unused) {
    (void)unused;
    SDL_LockMutex(g_mutex);
    while (!g_quit) {
        SDL_UnlockMutex(g_mutex);
        int ok = fetch_once();
        SDL_LockMutex(g_mutex);
        g_now = 0;
        /* every quarter of an hour, or a minute after a failure, or when asked */
        Sint64 wait_ms = (ok ? FETCH_EVERY_S : 60) * 1000;
        Uint64 until = SDL_GetTicks() + (Uint64)wait_ms;
        while (!g_quit && !g_now && SDL_GetTicks() < until)
            SDL_WaitConditionTimeout(g_cond, g_mutex, (Sint32)(until - SDL_GetTicks()));
    }
    SDL_UnlockMutex(g_mutex);
    return 0;
}

void realtime_start(double lat, double lon) {
    if (g_thread) return;
    g_lat = lat; g_lon = lon;
    g_quit = 0; g_now = 0; g_fresh = 0;
    g_mutex = SDL_CreateMutex();
    g_cond = SDL_CreateCondition();
    g_thread = SDL_CreateThread(fetch_thread, "weather", NULL);
    if (!g_thread) plat_log("real time: no thread (%s)", SDL_GetError());
}

void realtime_stop(void) {
    if (!g_thread) return;
    SDL_LockMutex(g_mutex);
    g_quit = 1;
    SDL_SignalCondition(g_cond);
    SDL_UnlockMutex(g_mutex);
    /* a request in flight can take its timeout: don't hold up the exit */
    SDL_DetachThread(g_thread);
    g_thread = NULL;
}

void realtime_refresh(void) {
    if (!g_thread) return;
    SDL_LockMutex(g_mutex);
    g_now = 1;
    SDL_SignalCondition(g_cond);
    SDL_UnlockMutex(g_mutex);
}

int realtime_poll(Weather *out, char *desc, int desc_cap) {
    if (!g_thread) return 0;
    int got = 0;
    SDL_LockMutex(g_mutex);
    if (g_fresh) {
        *out = g_weather;
        snprintf(desc, (size_t)desc_cap, "%s", g_desc);
        g_fresh = 0;
        got = 1;
    }
    SDL_UnlockMutex(g_mutex);
    return got;
}
