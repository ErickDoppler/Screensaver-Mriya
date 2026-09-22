/* The aircraft mesh and its livery atlas, both linked into the executable
 * (see CMakeLists.txt: tools/meshpack.c and tools/make_decals.py make them). */
#ifndef MR_MODEL_H
#define MR_MODEL_H
#include "mathx.h"

enum { PART_FUSELAGE = 0, PART_WING = 1, PART_ENGINE = 2, PART_FIN = 3, PART_STAB = 4,
       PART_GLASS = 5 /* the windscreen panes (tools/meshpack.c finds them) */ };

typedef struct Model {
    unsigned vao, vbo, ibo;
    int      index_count;
    float    center[3], half[3];      /* dequantisation of the positions */
    unsigned decal_tex;
    unsigned skin_tex;               /* the cheatline's edges (tools/make_bands.py) */
    int      decal_w, decal_h;
} Model;

int  model_load(Model *m);
void model_draw(const Model *m);
void model_free(Model *m);

/* One livery decal, projected sideways (along x) onto the airframe. */
typedef struct Decal {
    float zc, yc;            /* centre on the side of the aircraft, body metres */
    float w, h;              /* size, metres */
    float u0, v0, u1, v1;    /* where it is in the atlas */
    float side;              /* +1 painted on faces looking right, -1 left */
    float part;              /* which part it may land on */
    float xc;                /* the side test is relative to this x */
    float reach;             /* and only this far out from it */
    float angle;             /* rotation on the skin, radians */
    float axis;              /* 0 projected from the side, 1 from below */
} Decal;

#define MAX_DECALS 40
/* The paint scheme of UR-82060 as a list of decals. Returns the count. */
int model_livery(Decal *out, int cap);
#endif
