/* Where the camera is bolted to the airframe, and how it moves with it.
 *
 * Body frame, metres: x toward the right wingtip, y up, z aft; the nose is at
 * z = -40.6, the fin tops at y = 12.6, the wingtips at x = +-44.3. */
#ifndef MR_CAMERA_H
#define MR_CAMERA_H
#include "mathx.h"
#include "settings.h"
#include "flight.h"

typedef struct Mount {
    vec3  pos;           /* body frame */
    float yaw, pitch;    /* degrees; yaw 0 looks forward, positive to the right */
    float fov;           /* degrees, vertical, before the user's lens setting */
    int   external;      /* not on the airframe: aims at the aircraft */
    int   on_wing;       /* rides the wing's flex */
    float drift;         /* how far (degrees) the idle gaze wanders */
    int   has_alt;       /* a second view the same key toggles to: */
    float alt_yaw, alt_pitch;
} Mount;

typedef struct CamState {
    int    kind;
    int    deck[CAM_COUNT], deck_n, deck_pos, last_kind;
    rng_t  rng;
    float  since_change;
    float  look_yaw, look_pitch;    /* the user's look-around, radians */
    float  look_idle;               /* seconds since the user last looked */
    float  fade;                    /* 1 = black, for the cut between mounts */
    int    pending;                 /* the mount to cut to when faded */
    int    view;                    /* which of the mount's views: 0 main, 1 the other way,
                                     * 2 to the left, 3 to the right */
    float  zoom;                    /* the mouse wheel's: < 1 closer, > 1 wider */
    float  orbit_dist;              /* the globe camera's distance, metres */
    int    pending_flip;            /* the cut pending is only a turn round */
    float  t;
    float  shake_phase[6];
    int    debug;                   /* a mount given on the command line */
    Mount  debug_mount;
    /* outputs */
    dvec3  pos;                     /* world */
    basis3 basis;                   /* world orientation: x right, y up, z back */
    float  fov;                     /* radians */
    float  near_plane;
} CamState;

const Mount *camera_mount(int kind);
void camera_init(CamState *c, const Settings *s, unsigned seed, int first);
/* Cuts to the next mount from the deck (through a short fade). */
void camera_next(CamState *c, const Settings *s);
void camera_set(CamState *c, int kind);
/* Moves on to the mount's next view - the other way, left, right, back to
 * the main one - if it has them (through a short fade). Returns whether it
 * will. */
int  camera_toggle_view(CamState *c);
/* The view's name ("looking forward", ...), for the caption. */
const char *camera_view_name(const CamState *c);
/* The view the next press of the camera's key moves to. */
int  camera_next_view(const CamState *c);
/* The mouse wheel: zooms the lens, or on the globe camera moves in and out. */
void camera_wheel(CamState *c, float steps);
void camera_look(CamState *c, float dyaw, float dpitch);
/* Replaces the mount with one given in the body frame (for testing). */
void camera_debug(CamState *c, vec3 pos, float yaw, float pitch, float fov);
/* `idle` is set while nobody has touched anything for a while: only then
 * does the rotation clock run. */
void camera_update(CamState *c, const Settings *s, const Flight *f, float dt, int idle);
#endif
