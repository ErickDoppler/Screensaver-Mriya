/* The aircraft: a point-mass flight model of an An-225 of the set weight, the
 * autopilot that flies the racetrack, and the pilot who may take over.
 *
 * World frame: x east, y up (metres above sea level), z south. Headings are
 * radians clockwise from north. */
#ifndef MR_FLIGHT_H
#define MR_FLIGHT_H
#include "mathx.h"
#include "terrain.h"
#include "settings.h"

typedef struct FlightInput {
    float pitch;      /* -1 push .. +1 pull */
    float roll;       /* -1 left .. +1 right */
    float yaw;        /* -1 left .. +1 right (rudder) */
    float throttle;   /* -1 less .. +1 more; 0 leaves the lever alone */
    int   active;     /* a control touched this frame that takes the aircraft off the autopilot */
    int   no_resume;  /* the autopilot does not come back by itself (a joystick is flying) */
    int   has_throttle_abs;   /* a throttle lever (a joystick's): */
    float throttle_abs;       /* where it stands, 0..1 */
} FlightInput;

enum { AP_LEG = 0, AP_TURN = 1 };

typedef struct Flight {
    dvec3  pos;
    double speed;          /* true airspeed, m/s */
    double gamma;          /* flight path angle */
    double psi;            /* heading of the air velocity */
    double bank;
    double bank_target;    /* where the controls are taking the bank */
    double alpha;          /* angle of attack */
    double beta;           /* sideslip (rudder) */
    double n;              /* load factor actually flown */
    double n_cmd;
    double q;              /* pitch rate of the flight path the controls are flying, rad/s */
    double p;              /* roll rate the pilot is flying, rad/s */
    double protect;        /* 0..1: how much of the pitch the floor protection has taken */
    double throttle;       /* 0..1 lever */
    double thrust, drag;   /* newtons, for the HUD and the engines' look */
    double mass;
    double track;          /* ground track */
    double ground_speed;
    double vs;             /* vertical speed, m/s */
    double mach;
    double time;

    /* who is flying */
    int    manual;
    int    manual_throttle;
    double idle;           /* seconds since the pilot last touched anything */

    /* the racetrack */
    int    phase;
    dvec3  leg_start;
    double leg_dir;        /* track of the current leg */
    double leg_len;
    double turn_radius;
    dvec3  turn_center;
    double turned;         /* radians turned so far */
    double leg_flown;      /* metres along the current leg */
    int    legs_done;

    /* the altitude programme */
    double alt_target;
    double since_alt_change;   /* metres flown since the target was picked */
    double speed_target;

    /* limits */
    double floor_alt;          /* the soft floor at the moment */
    double floor_rate;         /* how fast it is rising under the aircraft, m/s */
    double floor_gamma;        /* the climb angle the ground ahead asks for now */
    double floor_bite;         /* 0..1: how hard the floor is overriding the controls */
    int    floor_active;       /* the floor is holding the aircraft up */
    double ceiling;            /* highest altitude it could sustain right now */
    double ground;             /* terrain height under the aircraft */
    int    stall_guard;        /* alpha protection is lowering the nose */

    /* what the eye sees */
    float  gust_n;             /* turbulence's share of the load factor */
    float  gust_roll, gust_yaw, gust_pitch;
    float  chop;               /* high-frequency shake, 0..1 */
    float  flex, flex_v;       /* wingtip deflection (m) and its rate */
    float  engine_rpm;         /* 0..1 */
    float  ail, elev, rud;     /* control surface deflections, radians */
    double ail_f, elev_f, rud_f;   /* where the actuators are heading */
    float  turb_state[8];
    rng_t  rng;
} Flight;

void flight_init(Flight *f, const Settings *s, unsigned seed, double altitude, int leg_eastbound);
/* Advances by dt seconds of simulated time (already scaled). `turb` 0..1,
 * `wind` in m/s blowing towards (x, z). */
void flight_update(Flight *f, const Settings *s, const TerrainParams *t,
                   const FlightInput *in, double dt, float turb, dvec3 wind);
/* Engages (1) or releases (0) the autopilot right away. */
void flight_set_autopilot(Flight *f, int on);
/* Gives the autopilot a new altitude to fly to (the scenery is changing). */
void flight_aim_altitude(Flight *f, double altitude);
/* Puts the aircraft at a new altitude instantly (used inside the passage). */
void flight_teleport_altitude(Flight *f, double altitude);
/* Body axes in the world: x right wing, y up, z aft (the nose is -z). */
dbasis3 flight_attitude(const Flight *f);
/* Nose pitch and heading, for the HUD. */
double flight_pitch(const Flight *f);
double flight_heading(const Flight *f);
#endif
