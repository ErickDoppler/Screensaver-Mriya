/* Contrails: where each engine's exhaust has been, kept in the frame of the
 * moving air so the trails drift with the wind once they are left behind. */
#ifndef MR_TRAILS_H
#define MR_TRAILS_H
#include "mathx.h"

#define TRAIL_ENGINES 6
#define TRAIL_SAMPLES 480          /* 72 s at one sample every 0.15 s */
#define TRAIL_STEP 0.15f

/* Engine nozzles in the body frame: the aft end of each nacelle on its axis. */
extern const vec3 trail_nozzle[TRAIL_ENGINES];

typedef struct Trails {
    dvec3 air[TRAIL_SAMPLES][TRAIL_ENGINES];   /* air-mass coordinates */
    float born[TRAIL_SAMPLES];                 /* time the sample was laid */
    float strength[TRAIL_SAMPLES];             /* how much it condensed, 0..1 */
    int   head, count;
    float since;
} Trails;

void trails_reset(Trails *t);
/* Lays a sample if it is time. `wind_off` is how far the air has moved. */
void trails_update(Trails *t, float time, float dt, dvec3 ac_pos, dbasis3 att,
                   dvec3 wind_off, float strength);
/* How strongly contrails form at an altitude, given the humidity (0..1). */
float trails_condensation(double altitude, float humidity);
#endif
