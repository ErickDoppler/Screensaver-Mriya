/* Stress test for the scenery changes: hundreds of changes, gradual and
 * quick, many interrupted halfway, and after each one settles, a check that
 * what is shown is what the scenario says - the kind, its sky, its ground.
 *
 *   gcc -O1 -Isrc tools/test_weather.c src/weather.c src/terrain.c src/settings.c -lm -o test_weather
 *
 * Exits non-zero on the first mismatch, printing what differed. */
#include "weather.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_quiet = 1;
void plat_log(const char *fmt, ...) {
    if (g_quiet) return;
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n");
}
void plat_log_set_file(const char *p) { (void)p; }
void plat_log_default(char *o, int c) { (void)c; o[0] = 0; }
int plat_store_read_int(const char *k, int *o) { (void)k; (void)o; return 0; }
int plat_store_write_int(const char *k, int v) { (void)k; (void)v; return 1; }
int plat_store_read_str(const char *k, char *o, int c) { (void)k; (void)o; (void)c; return 0; }
int plat_store_write_str(const char *k, const char *v) { (void)k; (void)v; return 1; }

static int fail(const char *what, int step, const WeatherState *w) {
    printf("FAIL at change %d: %s\n  kind %s  cur.biome %s  terrain.biome %s  t_to.biome %s  next.biome %s\n"
           "  sun %.1f (next %.1f)  wblend %.2f  tblend %.2f  passage %.2f  mix %.2f\n",
           step, what, settings_scene_name(w->kind), terrain_biome_name(w->cur.biome),
           terrain_biome_name(w->terrain.biome), terrain_biome_name(w->t_to.biome),
           terrain_biome_name(w->next.biome), w->cur.sun_elev, w->next.sun_elev,
           w->wblend, w->tblend, w->passage, w->terrain.biome_mix);
    return 1;
}

int main(int argc, char **argv) {
    Settings s;
    settings_defaults(&s);
    s.weather_minutes = 0;                    /* the test deals the changes itself */
    WeatherState w;
    weather_init(&w, &s, 12345u, -1);
    rng_t r = { 777u };
    dvec3 ac = dv3(0.0, 9000.0, 0.0);         /* high: the ground never holds a change back */
    int n = argc > 1 ? atoi(argv[1]) : 400;
    const float dt = 1.f / 30.f;
    int settled_checks = 0;
    for (int i = 0; i < n; ++i) {
        int quick = rng_int(&r, 3) == 0;
        weather_next(&w, &s, quick);
        /* sometimes interrupt it part way, and change again */
        float cut = rng_int(&r, 3) == 0 ? rng_range(&r, 0.5f, 40.f) : 1e9f;
        float t = 0.f;
        int interrupted = 0;
        while (w.wblend < 1.f || w.tblend < 1.f || w.passage_dir != 0) {
            weather_update(&w, &s, dt, ac);
            t += dt;
            if (t > cut) { interrupted = 1; break; }
            if (t > 600.f) return fail("never settled", i, &w);
        }
        if (interrupted) continue;
        weather_update(&w, &s, dt, ac);
        /* settled: everything must agree */
        settled_checks++;
        Weather want;
        rng_t tmp = { 1u };
        weather_make(&want, w.kind, &tmp);
        if (w.cur.biome != w.next.biome) return fail("the sky's biome is not the scenario's", i, &w);
        if (w.terrain.biome != w.cur.biome) return fail("the ground's biome is not the scenario's", i, &w);
        if (w.terrain.to && w.terrain.biome_mix > 0.f) return fail("the ground is still blending", i, &w);
        if (fabsf(w.cur.sun_elev - w.next.sun_elev) > 0.01f) return fail("the sun is not where the scenario puts it", i, &w);
        /* the sun within the range this scenario puts it in */
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 200; ++k) {
            rng_t rr = { (unsigned)(k * 2246822519u + 7u) };
            Weather cand;
            weather_make(&cand, w.kind, &rr);
            lo = fminf(lo, cand.sun_elev); hi = fmaxf(hi, cand.sun_elev);
        }
        (void)want;
        if (w.cur.sun_elev < lo - 0.5f || w.cur.sun_elev > hi + 0.5f)
            return fail("the sun is outside this scenario's range", i, &w);
        /* the biome must be one the scenario deals: a biome that no weather_make
         * of this kind can produce means something else's got through */
        int ok = 0;
        for (int k = 0; k < 200 && !ok; ++k) {
            rng_t rr = { (unsigned)(k * 2654435761u + 1u) };
            Weather cand;
            weather_make(&cand, w.kind, &rr);
            ok = cand.biome == w.cur.biome;
        }
        if (!ok) return fail("the biome is not one this scenario uses", i, &w);
    }
    printf("OK: %d changes, %d checked settled\n", n, settled_checks);
    return 0;
}
