/* Flight model.
 *
 * Point mass with the forces that matter for how a heavy freighter *moves*:
 * lift set by the load factor, drag polar, thrust that lapses with altitude
 * and Mach, gravity. Attitude comes from the flight path plus angle of
 * attack. The autopilot flies it through gentle, rate-limited load factor
 * and roll. The pilot flies it the way fly-by-wire lets a heavy jet be flown:
 * the stick asks for a pitch rate - 3 deg/s when slow, 10 deg/s from 540 km/h
 * indicated up, as far as the wing's lift allows - and a roll rate, which the
 * aircraft reaches with a little lag, and released it holds the flight path
 * and the bank. The weight is a setting (300 to 640 t).
 *
 * The ceiling is not a number anywhere in here. It is where the thrust left
 * over after drag runs out - about 13,500 m at the default 450 t, 11,000 m at
 * the 640 t maximum - so a pilot hauling back up there simply bleeds speed
 * until the alpha guard lowers the nose. */
#include "flight.h"
#include "platform.h"
#include <string.h>

#define G            9.80665
#define WING_AREA    905.0             /* m^2 */
#define CL0          0.25
#define CLA          5.4               /* per radian */
#define CLMAX        1.45              /* clean */
#define CD0          0.0205
#define K_INDUCED    0.046             /* 1 / (pi e AR), AR 8.6, e 0.8 */
#define THRUST_SL    1377000.0         /* 6 x D-18T, N */
#define FLOOR_AGL    50.0              /* the soft floor: this far above the ground */
#define RESUME_AGL   2000.0            /* the autopilot takes a low aircraft back up to this */
#define ALT_MIN      1000.0
#define ALT_MAX      11000.0
#define STEP         0.02              /* integration substep, s */
#define N_MAX        3.8               /* fly-by-wire load factor limits */
#define N_MIN        -1.0
#define N_RECOVER    2.3               /* what the floor protection pulls */
#define GAMMA_MAX    1.40              /* nearly straight up, or down */
#define GAMMA_UP     0.60              /* the steepest the floor will climb it: a zoom */
#define BANK_MAX     DEG2RAD(60.0)

/* --- the atmosphere (ISA) ------------------------------------------------ */
static void isa(double h, double *rho, double *a, double *sigma) {
    double T, p;
    if (h < 11000.0) {
        T = 288.15 - 0.0065 * h;
        p = 101325.0 * pow(T / 288.15, 5.25588);
    } else {
        T = 216.65;
        p = 22632.06 * exp(-(h - 11000.0) / 6341.62);
    }
    *rho = p / (287.053 * T);
    *a = sqrt(1.4 * 287.053 * T);
    *sigma = *rho / 1.225;
}

static double thrust_available(double sigma, double mach) {
    return THRUST_SL * pow(sigma, 0.8) * (1.0 - 0.45 * mach + 0.2 * mach * mach);
}

static double drag_at(double q, double cl, double mach) {
    double cd = CD0 + K_INDUCED * cl * cl;
    if (mach > 0.76) { double m = mach - 0.76; cd += 20.0 * m * m * m * m + 0.5 * m * m; }
    return q * WING_AREA * cd;
}

/* The airspeed the autopilot wants: 280 knots indicated down low, Mach 0.74
 * up high, whichever is slower - which is how a crew would fly it. */
static double schedule_speed(double h) {
    double rho, a, sigma;
    isa(h, &rho, &a, &sigma);
    double tas_cas = 144.0 / sqrt(sigma);
    double tas_mach = 0.74 * a;
    return tas_cas < tas_mach ? tas_cas : tas_mach;
}

/* Best sustained climb rate at an altitude, flying the schedule speed. */
static double climb_capability(double h, double mass) {
    double rho, a, sigma;
    isa(h, &rho, &a, &sigma);
    double v = schedule_speed(h);
    double q = 0.5 * rho * v * v;
    double cl = mass * G / (q * WING_AREA);
    double d = drag_at(q, cl, v / a);
    double t = thrust_available(sigma, v / a);
    return (t - d) * v / (mass * G);
}

static double wrap_pi(double a) { return wrapd(a, -MR_PI_D, MR_PI_D); }

/* Coloured noise with unit variance and correlation time tau: an
 * Ornstein-Uhlenbeck process, stepped exactly so it does not care about dt. */
static float gust(Flight *f, int k, float tau, double dt) {
    float white = (rng_f(&f->rng) + rng_f(&f->rng) + rng_f(&f->rng) - 1.5f) * 2.0f;
    float decay = expf(-(float)dt / tau);
    f->turb_state[k] = f->turb_state[k] * decay + sqrtf(1.f - decay * decay) * white;
    return f->turb_state[k];
}

static void pick_altitude(Flight *f) {
    double lo = fmax(ALT_MIN, f->floor_alt + 200.0), hi = ALT_MAX;
    if (lo > hi - 500.0) lo = hi - 500.0;
    f->alt_target = lo + (hi - lo) * rng_f(&f->rng);
    f->since_alt_change = 0.0;
    plat_log("autopilot: new altitude %.0f m", f->alt_target);
}

static void start_leg(Flight *f, double dir) {
    f->phase = AP_LEG;
    f->leg_dir = wrap_pi(dir);
    f->leg_start = f->pos;
    f->leg_flown = 0.0;
    f->turned = 0.0;
}

void flight_init(Flight *f, const Settings *s, unsigned seed, double altitude, int eastbound) {
    memset(f, 0, sizeof *f);
    f->rng.s = seed ? seed : 0xC0FFEEu;
    f->mass = s->mass_t * 1000.0;
    f->pos = dv3(eastbound ? -s->leg_km * 500.0 : s->leg_km * 500.0, altitude, 0.0);
    f->psi = eastbound ? MR_PI_D * 0.5 : -MR_PI_D * 0.5;
    f->track = f->psi;
    f->speed = schedule_speed(altitude);
    f->n = f->n_cmd = 1.0;
    f->throttle = 0.6;
    f->floor_alt = FLOOR_AGL;
    f->leg_len = s->leg_km * 1000.0;
    f->turn_radius = s->turn_km * 500.0;
    f->engine_rpm = 0.8f;
    start_leg(f, f->psi);
    f->alt_target = altitude;
    f->since_alt_change = 0.0;
}

void flight_aim_altitude(Flight *f, double altitude) {
    f->alt_target = clampd(altitude, f->floor_alt + 100.0, ALT_MAX);
    f->since_alt_change = 0.0;
    plat_log("autopilot: heading for %.0f m for the new scenery", f->alt_target);
}

void flight_teleport_altitude(Flight *f, double altitude) {
    f->pos.y = altitude;
    f->gamma = 0.0;
    f->speed = schedule_speed(altitude);
    f->alt_target = altitude;
    f->since_alt_change = 0.0;
}

/* --- the autopilot's lateral guidance ----------------------------------------
 * On a leg: steer to the leg's line with a lookahead, so a gust or a pilot's
 * excursion is taken out smoothly. In the turn: stay on the circle whose
 * diameter is the gap between the legs, always turning right. */
static double ap_bank(Flight *f, const Settings *s) {
    double v = f->ground_speed > 50.0 ? f->ground_speed : f->speed;
    double desired;
    double ff = 0.0;
    f->leg_len = s->leg_km * 1000.0;
    f->turn_radius = s->turn_km * 500.0;
    if (f->phase == AP_LEG) {
        dvec3 d = dv3_sub(f->pos, f->leg_start);
        double ux = sin(f->leg_dir), uz = -cos(f->leg_dir);   /* along the leg */
        double rx = cos(f->leg_dir), rz = sin(f->leg_dir);    /* to its right */
        f->leg_flown = d.x * ux + d.z * uz;
        double xte = d.x * rx + d.z * rz;
        desired = f->leg_dir - atan2(xte, 5000.0);
        if (f->leg_flown >= f->leg_len) {
            f->phase = AP_TURN;
            f->turn_center = dv3(f->pos.x + rx * f->turn_radius, 0.0, f->pos.z + rz * f->turn_radius);
            f->turned = 0.0;
            plat_log("autopilot: leg %d done after %.0f km, turning right", f->legs_done + 1,
                     f->leg_flown / 1000.0);
        }
    } else {
        double cx = f->pos.x - f->turn_center.x, cz = f->pos.z - f->turn_center.z;
        double r = sqrt(cx * cx + cz * cz);
        /* bearing of the aircraft from the centre, clockwise from north */
        double bearing = atan2(cx, -cz);
        double tangent = bearing + MR_PI_D * 0.5;          /* clockwise round the circle */
        desired = tangent + atan2(r - f->turn_radius, 3000.0);
        ff = atan(v * v / (G * f->turn_radius));
        f->turned = wrap_pi(f->track - f->leg_dir);
        if (f->turned < -0.2) f->turned += 2.0 * MR_PI_D;
        if (f->turned >= MR_PI_D - 0.02) {
            f->legs_done++;
            start_leg(f, f->leg_dir + MR_PI_D);
            plat_log("autopilot: turn complete, leg %d heading %.0f deg", f->legs_done + 1,
                     wrapd(f->leg_dir * 180.0 / MR_PI_D, 0.0, 360.0));
        }
    }
    double err = wrap_pi(desired - f->track);
    double bank = ff + 1.6 * err;
    double lim = DEG2RAD(25.0);
    return clampd(bank, -lim, lim);
}

static void engage(Flight *f) {
    f->manual = 0;
    f->manual_throttle = 0;
    /* A fresh leg along wherever the pilot left it pointing, at the altitude
     * they left it at: the racetrack carries on from here. Left low - under
     * 2000 m above the ground - it first climbs back up to that, at its
     * usual unhurried rate, and the altitude programme resumes from there. */
    start_leg(f, f->track);
    double low = fmax(f->ground, 0.0) + RESUME_AGL;
    f->alt_target = clampd(fmax(f->pos.y, low), f->floor_alt + 30.0, ALT_MAX);
    f->since_alt_change = 0.0;
    plat_log("autopilot engaged: heading %.0f deg, %.0f m",
             wrapd(f->track * 180.0 / MR_PI_D, 0.0, 360.0), f->alt_target);
}

void flight_set_autopilot(Flight *f, int on) {
    if (on && f->manual) engage(f);
    else if (!on && !f->manual) {
        f->manual = 1;
        f->idle = 0.0;
        plat_log("autopilot released");
    }
}

void flight_update(Flight *f, const Settings *s, const TerrainParams *t,
                   const FlightInput *in, double dt_total, float turb, dvec3 wind) {
    f->mass = s->mass_t * 1000.0;       /* the weight is a setting */

    /* --- who has the controls ------------------------------------------ */
    if (in->active && s->manual_flight) {
        if (!f->manual) plat_log("pilot has the controls");
        f->manual = 1;
        f->idle = 0.0;
        if (in->throttle != 0.f) f->manual_throttle = 1;
    } else {
        f->idle += dt_total;
        if (f->manual && !in->no_resume && f->idle > s->autopilot_resume) engage(f);
    }
    /* a throttle lever is the pilot's as soon as the pilot flies */
    if (f->manual && in->has_throttle_abs) f->manual_throttle = 1;

    /* --- the ground and the floor ----------------------------------------
     * The floor follows the terrain FLOOR_AGL above it - but a heavy jet
     * cannot hop a ridge, so it is also raised ahead of every rise by what
     * the aircraft could climb in the distance left: sampled along the track
     * for 12 km, the floor here is the lowest height from which each sample
     * can still be cleared at the climb angle the aircraft has - its engines'
     * and, the faster it goes, a zoom on its speed. The climb angle each
     * sample asks for right now is kept too. Water counts
     * as ground at sea level. Looked up ten times a second: the terrain
     * function is not free and the ground does not move. */
    {
        static double since_ground = 1e9;
        since_ground += dt_total;
        if (since_ground > 0.1) {
            double elapsed = since_ground;
            since_ground = 0.0;
            f->ground = fmax(terrain_height(t, f->pos.x, f->pos.z, 40.f), 0.0);
            double v = f->speed;
            double g_eng = asin(clampd(climb_capability(f->pos.y, f->mass) / v, 0.0, 0.9));
            double g_av = clampd(fmax(g_eng + 0.04, (v - 110.0) / 900.0), 0.06, 0.25);
            double g_need = -1.0;
            double sx = sin(f->track), sz = -cos(f->track);
            double floor_alt = f->ground + FLOOR_AGL;
            for (int k = 1; k <= 48; ++k) {
                double d = k * 250.0;
                double h = 0.0;
                for (int l = -1; l <= 1; ++l) {         /* a swath, not a line */
                    double gx = f->pos.x + sx * d - sz * l * 150.0, gz = f->pos.z + sz * d + sx * l * 150.0;
                    h = fmax(h, terrain_height(t, gx, gz, 40.f));
                }
                floor_alt = fmax(floor_alt, h + FLOOR_AGL - d * tan(g_av));
                g_need = fmax(g_need, atan((fmax(h, 0.0) + FLOOR_AGL - f->pos.y) / d));
            }
            f->floor_gamma = g_need;
            /* how fast the floor is rising under the aircraft, smoothed */
            double rate = (floor_alt - f->floor_alt) / elapsed;
            if (elapsed > 1.0) rate = 0.0;
            f->floor_rate = approachd(f->floor_rate, clampd(rate, -60.0, 60.0), 0.6, elapsed);
            f->floor_alt = floor_alt;
        }
    }

    int steps = (int)ceil(dt_total / STEP);
    if (steps < 1) steps = 1;
    if (steps > 400) steps = 400;
    double dt = dt_total / steps;
    for (int step = 0; step < steps; ++step) {
        double rho, a, sigma;
        isa(f->pos.y, &rho, &a, &sigma);
        double v = f->speed;
        double q = 0.5 * rho * v * v;
        double w = f->mass * G;
        f->mach = v / a;
        double n_stall = CLMAX * q * WING_AREA / w;      /* most lift available */

        /* --- turbulence -------------------------------------------------- */
        /* Calm air is calm: hundreds of tonnes and an 88 m wing average the small
         * eddies away, so nothing moves until the weather is rough, and even
         * then the airframe answers slowly - long, heavy swells, and only in
         * real turbulence a shudder on top. */
        float tq = turb * turb;
        float gv = gust(f, 0, 2.5f, dt) * 3.0f * tq;     /* vertical gust, m/s */
        f->gust_roll  = gust(f, 1, 3.5f, dt) * DEG2RAD(3.0f * tq);
        f->gust_yaw   = gust(f, 2, 4.0f, dt) * DEG2RAD(0.8f * tq);
        f->gust_pitch = gust(f, 3, 2.5f, dt) * DEG2RAD(0.6f * tq);
        /* a gust changes the angle of attack, and so the lift, before the
         * aircraft can do anything about it */
        f->gust_n = (float)(CLA * (gv / v) * q * WING_AREA / w);
        f->chop = smoothstepf(0.45f, 1.f, turb);
        f->chop *= f->chop;

        /* --- targets ------------------------------------------------------ */
        double bank_cmd, gamma_cmd;
        f->speed_target = schedule_speed(f->pos.y);
        f->ceiling = 0.0;
        double climb_max = climb_capability(f->pos.y, f->mass);

        /* How hard the aircraft may pull right now: the fly-by-wire limit,
         * and never more lift than the wing has. */
        double n_max = fmin(N_MAX, n_stall / 1.1);
        double v_ias = v * sqrt(sigma);     /* the rates are scheduled on indicated speed */
        double cb = cos(f->bank);

        if (!f->manual) {
            f->since_alt_change += v * dt;
            if (f->since_alt_change >= s->altitude_change_km * 1000.0) pick_altitude(f);
            if (f->alt_target < f->floor_alt) f->alt_target = f->floor_alt + 100.0;
            bank_cmd = ap_bank(f, s);
            double err = f->alt_target - f->pos.y;
            double vs_cmd = err * 0.015;
            /* unhurried - unless it has a long way to climb */
            double up = fmin(err > 2500.0 ? 15.0 : 8.0, fmax(0.3, climb_max * 0.85));
            vs_cmd = clampd(vs_cmd, -11.0, up);
            /* don't dive faster than the engines at idle can keep the speed */
            if (v > f->speed_target + 8.0 && vs_cmd < 0.0) vs_cmd *= 0.5;
            gamma_cmd = asin(clampd(vs_cmd / v, -0.2, 0.2));
            double n_level = cos(f->gamma) / cb;
            f->n_cmd = n_level + (gamma_cmd - f->gamma) * v / G * 0.9;
            /* gentle in the cruise; firmer when it has taken over from a
             * pilot who left it climbing or diving steeply */
            double auth = 0.15 + 0.45 * smoothstepd(0.1, 0.5, fabs(gamma_cmd - f->gamma));
            f->n_cmd = clampd(f->n_cmd, 1.0 - auth, 1.0 + auth);
            f->q = G / v * (f->n_cmd * cb - cos(f->gamma));
            f->p = 0.0;
        } else {
            /* The pilot asks for a pitch rate of the flight path, more of it
             * the faster the aircraft goes, and gets it after a moment's lag -
             * the inertia of a long fuselage. Released, the stick holds the
             * path it is on. */
            double qmax = DEG2RAD(3.0 + 7.0 * clampd((v_ias - 83.3) / (150.0 - 83.3), 0.0, 1.0));
            double lag = 1.0 - exp(-dt / 0.45);
            f->q += (in->pitch * qmax - f->q) * lag;
            f->n_cmd = (v * f->q / G + cos(f->gamma)) / fmax(cb, 0.3);
            /* roll rate the same way: 6 deg/s slow, 15 deg/s fast */
            double pmax = DEG2RAD(6.0 + 9.0 * clampd((v_ias - 83.3) / (150.0 - 83.3), 0.0, 1.0));
            f->p += (in->roll * pmax - f->p) * (1.0 - exp(-dt / 0.35));
            bank_cmd = clampd(f->bank + f->p * dt, -BANK_MAX, BANK_MAX);
            gamma_cmd = f->gamma;
            f->alt_target = f->pos.y;
        }

        /* --- the soft floor ---------------------------------------------
         * It looks ahead: closing on the floor as it is - the aircraft
         * descending, or the ground rising - how much height would a firm
         * pull-out at N_RECOVER cost? As that height runs out it takes the
         * pitch over from the pilot, gradually, rolls the wings level if it
         * has to pull, and rounds out onto the floor and follows it. It only
         * ever adds climb: the pilot may fly as low as the floor and as high
         * as they like. */
        {
            double margin = f->pos.y - f->floor_alt;
            double n_rec = fmin(N_RECOVER, n_max);
            /* the dive relative to the floor: against the ground's rise under
             * it, or the climb the ground ahead asks for */
            double g_rel = fmin(asin(clampd((f->vs - f->floor_rate) / v, -1.0, 1.0)),
                                f->gamma - fmax(f->floor_gamma, 0.0));
            /* ground rising ahead, or already under the floor: an escape, with
             * all the lift the wing has */
            if (margin < 0.0 || f->floor_gamma > 0.0) n_rec = n_max;
            double h_rec = g_rel < 0.0
                ? v * v / (G * fmax(n_rec - 1.0, 0.2)) * (1.0 - cos(g_rel)) + fabs(f->vs - f->floor_rate) * 0.8
                : 0.0;
            double want = 0.0;
            if (g_rel < 0.02) want = smoothstepd(h_rec * 1.2 + 250.0, h_rec * 1.2 + 30.0, margin);
            /* the ground ahead needs a climb steeper than the one flown */
            if (f->floor_gamma > 0.0 && f->floor_gamma > f->gamma - 0.02) want = fmax(want, smoothstepd(400.0, 100.0, margin));
            if (margin < 0.0) want = 1.0;
            /* hand over quickly, hand back slowly */
            f->protect = approachd(f->protect, want, want > f->protect ? 0.2 : 2.0, dt);
            double bite = 0.0;
            if (f->protect > 0.001) {
                /* the path onto the floor: settle down onto it from above,
                 * climb back to it from below, and ride it as it rises */
                double g_floor = asin(clampd(f->floor_rate / v, -0.3, 0.3));
                double g_t = asin(clampd(-margin * 0.04 / v, -0.12, 0.3));
                if (margin < 150.0) g_t = fmax(g_t, g_floor + asin(clampd(-margin * 0.04 / v, -0.1, 0.3)));
                /* whatever the ground ahead asks for, and a little more */
                if (margin < 400.0 && f->floor_gamma > 0.0) g_t = fmax(g_t, f->floor_gamma + 0.03);
                /* under the floor: climb out as hard as it can */
                if (margin < 0.0) g_t = fmax(g_t, asin(clampd(-margin * 0.12 / v, 0.0, 0.9)));
                g_t = fmin(g_t, GAMMA_UP);
                double gd_max = G * (n_rec - 1.0) / v;
                double gd_p = clampd((g_t - f->gamma) * 0.9, -gd_max, gd_max);
                double gd = G / v * (f->n_cmd * cb - cos(f->gamma));
                double gd_new = gd + (fmax(gd, gd_p) - gd) * f->protect;
                /* how much it is actually overriding the pilot */
                bite = f->protect * smoothstepd(0.0, 0.02, gd_p - gd);
                bank_cmd = bank_cmd * (1.0 - bite);
                f->n_cmd = (v * gd_new / G + cos(f->gamma)) / fmax(cb, 0.3);
                if (f->manual) f->q = f->q + (fmax(f->q, gd_new) - f->q) * f->protect;
            }
            f->floor_bite = bite;
            int active = bite > 0.5;
            if (!f->floor_active && active) plat_log("soft floor: taking over at %.0f m (%.0f m above the ground), %.0f m/s down",
                                                     f->pos.y, f->pos.y - f->ground, -f->vs);
            f->floor_active = active;
        }

        /* --- alpha and load protection ---------------------------------- */
        f->stall_guard = f->n_cmd > n_max;
        f->n_cmd = clampd(f->n_cmd, N_MIN, n_max);

        /* --- the aircraft responds -------------------------------------- */
        double n_rate = f->manual || f->floor_bite > 0.1 ? 6.0 : 0.18;
        f->n = rate_limit(f->n, f->n_cmd, n_rate, dt);
        double roll_rate_lim = f->manual ? DEG2RAD(20.0) : DEG2RAD(2.5);
        if (f->floor_bite > 0.1) roll_rate_lim = DEG2RAD(10.0);
        double target_bank = bank_cmd;
        f->bank_target = target_bank;
        f->bank = rate_limit(f->bank, target_bank, roll_rate_lim, dt);
        f->bank += f->gust_roll * dt * 0.3;

        /* --- thrust ------------------------------------------------------- */
        double n_eff = f->n + f->gust_n;
        double cl = n_eff * w / (q * WING_AREA);
        f->alpha = (cl - CL0) / CLA;
        f->drag = drag_at(q, cl, f->mach);
        double t_avail = thrust_available(sigma, f->mach);
        if (!f->manual_throttle) {
            double need = f->drag + w * sin(f->gamma) + (f->speed_target - v) * f->mass * 0.08;
            f->throttle = rate_limit(f->throttle, clampd(need / t_avail, 0.04, 1.0), 0.06, dt);
        } else if (in->has_throttle_abs) {
            /* a lever: the engines follow it at the rate they spool */
            f->throttle = rate_limit(f->throttle, clampd(in->throttle_abs, 0.04, 1.0), 0.25, dt);
        } else {
            f->throttle = clampd(f->throttle + in->throttle * 0.15 * dt, 0.04, 1.0);
        }
        f->thrust = f->throttle * t_avail;
        f->engine_rpm = (float)approachd(f->engine_rpm, 0.55 + 0.45 * f->throttle, 3.0, dt);

        /* --- equations of motion ----------------------------------------- */
        double dv = (f->thrust - f->drag) / f->mass - G * sin(f->gamma);
        double dgamma = G / v * (n_eff * cos(f->bank) - cos(f->gamma));
        double dpsi = G * n_eff * sin(f->bank) / (v * cos(f->gamma));
        /* rudder: a little sideslip and a slow skidding turn */
        f->beta = approachd(f->beta, DEG2RAD(4.0) * in->yaw * (f->manual ? 1.0 : 0.0), 1.5, dt);
        dpsi += -f->beta * 0.08 * v / 100.0;

        f->speed = fmax(60.0, v + dv * dt);
        f->gamma = clampd(f->gamma + dgamma * dt, -GAMMA_MAX, GAMMA_MAX);
        f->psi = wrap_pi(f->psi + dpsi * dt);

        double cg = cos(f->gamma);
        double vx = f->speed * cg * sin(f->psi) + wind.x;
        double vz = -f->speed * cg * cos(f->psi) + wind.z;
        double vy = f->speed * sin(f->gamma);
        f->pos.x += vx * dt;
        f->pos.y += vy * dt;
        f->pos.z += vz * dt;
        f->vs = vy;
        f->ground_speed = sqrt(vx * vx + vz * vz);
        f->track = atan2(vx, -vz);
        f->time += dt;

        /* --- the wings ---------------------------------------------------
         * A spring about 0.9 Hz, lightly damped, driven by the load factor.
         * The model was built with its wings at rest, so only the change
         * from one g shows - which is exactly what a passenger sees. */
        {
            float k = (float)((2.0 * MR_PI_D * 0.9) * (2.0 * MR_PI_D * 0.9));
            float c = 2.f * 0.08f * sqrtf(k);
            float target = (float)(n_eff - 1.0) * 2.4f;
            float acc = k * (target - f->flex) - c * f->flex_v;
            f->flex_v += acc * (float)dt;
            f->flex += f->flex_v * (float)dt;
        }
    }

    /* The control surfaces, as a heavy jet's move: slowly and smoothly, on
     * hydraulic actuators through a filter, and the faster it flies the less
     * they need to move for the same effect - in the cruise a few degrees
     * do it all, and only slow does a full input show big deflections. The
     * aileron works the roll still to come, the elevator the pitch still to
     * come (a little up, for trim), the rudder the pedals and the yaw damper;
     * gusts only reach them in weather rough enough to feel. */
    {
        double v = f->speed;
        double ks = clampd((95.0 / v) * (95.0 / v), 0.2, 1.0);
        double ail_cmd, elev_cmd, rud_cmd;
        if (f->manual) ail_cmd = in->roll * 0.9;
        else ail_cmd = clampd((f->bank_target - f->bank) * 3.0, -0.6, 0.6);
        ail_cmd -= f->gust_roll * 1.5;
        double rho, snd, sigma;
        isa(f->pos.y, &rho, &snd, &sigma);
        double v_ias = v * sqrt(sigma);
        double qmax = DEG2RAD(3.0 + 7.0 * clampd((v_ias - 83.3) / (150.0 - 83.3), 0.0, 1.0));
        elev_cmd = f->manual ? -clampd(in->pitch * 0.7 + (in->pitch * qmax - f->q) / qmax * 0.5, -1.0, 1.0)
                             : -clampd((f->n_cmd - f->n) * 3.0, -0.6, 0.6);
        elev_cmd += f->gust_n * 0.8 - 0.1;
        rud_cmd = in->yaw * (f->manual ? 0.9 : 0.0) + f->beta * 1.5 - f->gust_yaw * 1.5;
        double a = 1.0 - exp(-dt_total / 0.25);
        f->ail_f += (clampd(ail_cmd, -1.0, 1.0) * DEG2RAD(18.0) * ks - f->ail_f) * a;
        f->elev_f += (clampd(elev_cmd, -1.0, 1.0) * DEG2RAD(14.0) * ks - f->elev_f) * a;
        f->rud_f += (clampd(rud_cmd, -1.0, 1.0) * DEG2RAD(15.0) * ks - f->rud_f) * a;
        double rate = DEG2RAD(25.0) * dt_total;
        f->ail = (float)rate_limit(f->ail, f->ail_f, rate, 1.0);
        f->elev = (float)rate_limit(f->elev, f->elev_f, rate, 1.0);
        f->rud = (float)rate_limit(f->rud, f->rud_f, rate, 1.0);
    }

    /* The highest altitude it could sustain: where the climb capability runs
     * out. Only for the HUD, so a coarse search is fine. */
    {
        double lo = f->pos.y, hi = 14000.0;
        if (climb_capability(lo, f->mass) <= 0.0) hi = lo, lo = 0.0;
        for (int k = 0; k < 18; ++k) {
            double mid = 0.5 * (lo + hi);
            if (climb_capability(mid, f->mass) > 0.5) lo = mid; else hi = mid;
        }
        f->ceiling = lo;
    }
}

dbasis3 flight_attitude(const Flight *f) {
    /* velocity frame */
    double sp = sin(f->psi), cp = cos(f->psi);
    double sg = sin(f->gamma), cg = cos(f->gamma);
    dvec3 fwd = dv3(cg * sp, sg, -cg * cp);
    dvec3 right_h = dv3(cp, 0.0, sp);
    dvec3 up = dv3_cross(right_h, fwd);
    /* bank about the velocity */
    double sb = sin(f->bank + f->gust_roll * 0.3), cb = cos(f->bank + f->gust_roll * 0.3);
    dvec3 right = dv3_sub(dv3_scale(right_h, cb), dv3_scale(up, sb));
    dvec3 up2 = dv3_add(dv3_scale(up, cb), dv3_scale(right_h, sb));
    /* nose up by the angle of attack, about the wing */
    double al = f->alpha + f->gust_pitch * 0.3;
    double sa = sin(al), ca = cos(al);
    dvec3 fwd2 = dv3_add(dv3_scale(fwd, ca), dv3_scale(up2, sa));
    dvec3 up3 = dv3_sub(dv3_scale(up2, ca), dv3_scale(fwd, sa));
    /* sideslip and yaw gusts about the up axis */
    double be = f->beta + f->gust_yaw * 0.3;
    double sbe = sin(be), cbe = cos(be);
    dvec3 fwd3 = dv3_add(dv3_scale(fwd2, cbe), dv3_scale(right, sbe));
    dvec3 right3 = dv3_sub(dv3_scale(right, cbe), dv3_scale(fwd2, sbe));
    dbasis3 b = { dv3_norm(right3), dv3_norm(up3), dv3_norm(dv3_scale(fwd3, -1.0)) };
    return b;
}

double flight_pitch(const Flight *f) {
    dbasis3 b = flight_attitude(f);
    return asin(clampd(-b.z.y, -1.0, 1.0));
}

double flight_heading(const Flight *f) {
    dbasis3 b = flight_attitude(f);
    return atan2(-b.z.x, b.z.z);
}
