/* Real time: fly in the daylight and the weather of a real place, now.
 *
 * The sun and the moon come from the clock and the place's coordinates (low
 * precision astronomy, a fraction of a degree - plenty for a sky). The
 * weather comes from Open-Meteo (open-meteo.com, free, no key): the current
 * conditions, fetched off the main thread every quarter of an hour and
 * turned into a scenario. Only the place's name (when it is looked up) and
 * its coordinates leave the machine. */
#ifndef MR_REALTIME_H
#define MR_REALTIME_H
#include <time.h>
#include "weather.h"

/* The sun's and the moon's elevation and azimuth (degrees, azimuth from north
 * clockwise) at a place and a time, and the moon's lit fraction (0..1). */
void realtime_sky(double lat, double lon, time_t utc, float *sun_elev, float *sun_azim,
                  float *moon_elev, float *moon_azim, float *moon_phase);

/* Looks a place up by name. Blocking (a second or two). Fills a display name
 * like "Kyiv, Ukraine" and the coordinates; returns 1 if found. */
int realtime_geocode(const char *query, char *name, int name_cap, double *lat, double *lon);

/* The live weather, fetched in the background. */
void realtime_start(double lat, double lon);
void realtime_stop(void);
/* Returns 1 (once) when fresh conditions have arrived since the last call,
 * with a Weather made from them and a line describing them. */
int  realtime_poll(Weather *out, char *desc, int desc_cap);
/* Asks for a fetch now (the user pressed the weather key). */
void realtime_refresh(void);
#endif
