#include "trails.h"
#include <string.h>

/* Engine axes from the mesh (x, y) and the tip of each core plug (z): the
 * intake lip plus 6.8 m. */
const vec3 trail_nozzle[TRAIL_ENGINES] = {
    { -24.595f, -0.835f,  -0.79f }, { -17.275f, -0.56f,  -5.64f }, {  -9.895f, -0.296f, -10.44f },
    {   9.895f, -0.296f, -10.44f }, {  17.275f, -0.56f,  -5.64f }, {  24.595f, -0.835f,  -0.79f },
};

void trails_reset(Trails *t) { memset(t, 0, sizeof *t); }

/* The Schmidt-Appleman criterion, loosely: jet exhaust condenses once the air
 * is colder than about -40 C, which in a standard atmosphere is from 8 km or
 * so up; damp air lowers the threshold and makes the trails persist. */
float trails_condensation(double altitude, float humidity) {
    float lo = 8600.f - humidity * 1200.f;
    float c = smoothstepf(lo - 600.f, lo + 400.f, (float)altitude);
    return c * (0.45f + 0.55f * humidity);
}

void trails_update(Trails *t, float time, float dt, dvec3 ac_pos, dbasis3 att,
                   dvec3 wind_off, float strength) {
    t->since += dt;
    if (t->since < TRAIL_STEP && t->count > 0) return;
    t->since = 0.f;
    int i = t->head;
    for (int e = 0; e < TRAIL_ENGINES; ++e) {
        dvec3 w = dv3_add(ac_pos, dbasis_apply(att, v3_to_dv3(trail_nozzle[e])));
        t->air[i][e] = dv3_sub(w, wind_off);
    }
    t->born[i] = time;
    t->strength[i] = strength;
    t->head = (t->head + 1) % TRAIL_SAMPLES;
    if (t->count < TRAIL_SAMPLES) t->count++;
}
