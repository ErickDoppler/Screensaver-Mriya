#include "camera.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

#define FADE_TIME 0.35f       /* seconds to black and back for a cut */
#define LOOK_RETURN 12.f      /* seconds before the gaze eases back */

/* The mounts, indexed by CAM_*. */
static const Mount mounts[CAM_COUNT] = {
    /* CAM_NOSE: on top of the nose, just ahead of the windscreen */
    { {  0.0f,  3.35f, -37.2f },    0.f,  -7.f, 68.f, 0, 0, 5.f, 0, 0.f, 0.f },
    /* CAM_BELLY: under the right wing between the first and second engines,
     * looking forward - or back under the wing */
    { { 13.6f,  0.55f,  -8.5f },   -8.f,  -4.f, 74.f, 0, 1, 6.f, 1, 172.f, -4.f },
    /* CAM_SPINE_AFT: on the spine behind the wing, looking at the tail - or
     * forward over the wing roots to the cockpit */
    { {  0.0f,  5.15f,   8.0f },  180.f,  -5.f, 66.f, 0, 0, 8.f, 1, 0.f, -6.f },
    /* CAM_FIN_TOP: on the tip of the right fin, down the whole aircraft */
    { { 15.0f, 13.0f,  40.0f },    -3.f, -13.f, 64.f, 0, 0, 5.f, 0, 0.f, 0.f },
    /* CAM_WINGTIP: right wingtip, looking in at the engines and the fuselage
     * - or back along the wing to the tail */
    { { 43.3f,  0.45f,   9.6f },  -78.f,  -3.f, 70.f, 0, 1, 7.f, 1, -148.f, -3.f },
    /* CAM_COCKPIT: on the cockpit roof, looking back over the whole top - or
     * forward over the nose */
    { {  0.0f,  4.25f, -30.5f },  180.f,  -7.f, 68.f, 0, 0, 7.f, 1, 0.f, -3.f },
    /* CAM_WINDOW: a side window ahead of the wing, looking back along it */
    { {  4.75f, 1.8f, -21.0f },   142.f,  -6.f, 64.f, 0, 0, 6.f, 1, 38.f, -6.f },
    /* CAM_CHIN: under the nose, looking back along the belly */
    { {  0.0f, -4.15f, -31.0f },  180.f,   4.f, 72.f, 0, 0, 7.f, 1, 0.f, -8.f },
    /* CAM_ENGINE: behind and beside the inboard right engine, looking
     * forward past it - or back along the flaps to the tail */
    { { 12.2f, -0.9f,   0.5f },   -12.f,   3.f, 70.f, 0, 1, 5.f, 1, 168.f, 1.f },
    /* CAM_STAB: above the left tailplane near its tip, looking forward */
    { {-13.2f,  5.5f,  37.5f },   12.f,  -4.f, 66.f, 0, 0, 6.f, 0, 0.f, 0.f },
    /* CAM_CHASE: a chase aircraft behind, above and a little to the left */
    { {-38.0f, 28.0f, 175.0f },    0.f,   0.f, 42.f, 1, 0, 3.f, 0, 0.f, 0.f },
    /* CAM_WINGMAN: a wingman off the left side, slightly ahead */
    { {-135.0f, 8.0f, -35.0f },    0.f,   0.f, 45.f, 1, 0, 3.f, 0, 0.f, 0.f },
    /* CAM_GLOBE: free orbit round the middle of the aircraft (placed in
     * camera_update; the mouse steers it, the wheel sets the distance) */
    { {   0.0f, 1.5f,   0.0f },    0.f,   0.f, 50.f, 1, 0, 0.f, 0, 0.f, 0.f },
};

static Mount g_debug_mount;
static int g_debug;

void camera_debug(CamState *c, vec3 pos, float yaw, float pitch, float fov) {
    g_debug_mount.pos = pos;
    g_debug_mount.yaw = yaw;
    g_debug_mount.pitch = pitch;
    g_debug_mount.fov = fov;
    g_debug_mount.external = 0;
    g_debug_mount.on_wing = 0;
    g_debug_mount.drift = 0.f;
    g_debug = 1;
    c->debug = 1;
    c->debug_mount = g_debug_mount;
}

const Mount *camera_mount(int kind) {
    if (g_debug) return &g_debug_mount;
    return &mounts[kind >= 0 && kind < CAM_COUNT ? kind : 0];
}

static void reshuffle(CamState *c, const Settings *s) {
    c->deck_n = 0;
    for (int i = 0; i < CAM_COUNT; ++i)
        if (settings_camera_enabled(s, i)) c->deck[c->deck_n++] = i;
    if (c->deck_n == 0) c->deck[c->deck_n++] = CAM_NOSE;
    for (int i = c->deck_n - 1; i > 0; --i) {
        int j = rng_int(&c->rng, i + 1);
        int t = c->deck[i]; c->deck[i] = c->deck[j]; c->deck[j] = t;
    }
    if (c->deck_n > 1 && c->deck[0] == c->last_kind) {
        int t = c->deck[0]; c->deck[0] = c->deck[1]; c->deck[1] = t;
    }
    c->deck_pos = 0;
}

static int deal(CamState *c, const Settings *s) {
    if (c->deck_pos >= c->deck_n) reshuffle(c, s);
    return c->deck[c->deck_pos++];
}

/* A mount's views. Bit 0 turns it the other way; bit 1, on a mount off
 * the centreline, moves it to the same place on the aircraft's other side
 * (mirrored), so a side camera has four: this side and that, each looking
 * both ways. On the centreline there are only the two directions. */
static int mirrored_mount(const Mount *m) { return fabsf(m->pos.x) > 0.5f; }
static int view_count(const Mount *m) { return !m->has_alt ? 1 : (mirrored_mount(m) ? 4 : 2); }

static void view_dir(const Mount *m, int view, float *yaw, float *pitch) {
    int other = m->has_alt && (view & 1);
    *yaw = other ? m->alt_yaw : m->yaw;
    *pitch = other ? m->alt_pitch : m->pitch;
    if (view & 2) *yaw = -*yaw;          /* the other side: mirrored */
}

static int view_side_left(const Mount *m, int view) {
    int left = m->pos.x < 0.f;
    return (view & 2) ? !left : left;
}

const char *camera_view_name(const CamState *c) {
    static char buf[64];
    const Mount *m = camera_mount(c->kind);
    float yaw, pitch;
    view_dir(m, c->view, &yaw, &pitch);
    float a = fabsf(fmodf(yaw + 540.f, 360.f) - 180.f);   /* 0 forward .. 180 back */
    const char *dir = a < 90.f ? "looking forward" : "looking back";
    if (!mirrored_mount(m)) return dir;
    snprintf(buf, sizeof buf, "%s side, %s", view_side_left(m, c->view) ? "left" : "right", dir);
    return buf;
}

int camera_next_view(const CamState *c) {
    int n = view_count(camera_mount(c->kind));
    return n > 1 ? (c->view + 1) % n : 0;
}

void camera_wheel(CamState *c, float steps) {
    if (c->kind == CAM_GLOBE) {
        c->orbit_dist = clampf(c->orbit_dist * powf(0.88f, steps), 45.f, 600.f);
    } else {
        c->zoom = clampf(c->zoom * powf(0.9f, steps), 0.25f, 1.6f);
    }
    c->look_idle = 0.f;
}

int camera_toggle_view(CamState *c) {
    if (c->pending >= 0 || !camera_mount(c->kind)->has_alt) return 0;
    c->pending = c->kind;
    c->pending_flip = 1;
    return 1;
}

void camera_set(CamState *c, int kind) {
    c->kind = kind;
    c->view = 0;
    c->zoom = 1.f;
    if (c->orbit_dist <= 0.f) c->orbit_dist = 120.f;
    c->last_kind = kind;
    c->since_change = 0.f;
    c->look_yaw = c->look_pitch = 0.f;
    c->pending = -1;
    plat_log("camera: %s", settings_camera_name(kind));
}

void camera_init(CamState *c, const Settings *s, unsigned seed, int first) {
    memset(c, 0, sizeof *c);
    c->rng.s = seed ? seed : 0xCA3E7Au;
    c->last_kind = -1;
    c->pending = -1;
    reshuffle(c, s);
    int kind = first >= 0 ? first : deal(c, s);
    camera_set(c, kind);
    for (int i = 0; i < 6; ++i) c->shake_phase[i] = rng_range(&c->rng, 0.f, 100.f);
}

void camera_next(CamState *c, const Settings *s) {
    if (c->pending >= 0) return;
    int next = deal(c, s);
    if (next == c->kind && c->deck_n > 1) next = deal(c, s);
    c->pending = next;
}

void camera_look(CamState *c, float dyaw, float dpitch) {
    if (c->kind == CAM_GLOBE) {
        /* all the way round, and nearly over the top and under the belly */
        c->look_yaw = wrapf(c->look_yaw + dyaw, -MR_PI, MR_PI);
        c->look_pitch = clampf(c->look_pitch + dpitch, DEG2RAD(-80.f), DEG2RAD(80.f));
        c->look_idle = 0.f;
        return;
    }
    c->look_yaw = clampf(c->look_yaw + dyaw, DEG2RAD(-150.f), DEG2RAD(150.f));
    c->look_pitch = clampf(c->look_pitch + dpitch, DEG2RAD(-70.f), DEG2RAD(70.f));
    c->look_idle = 0.f;
}

/* Smooth wandering in [-1, 1]: a few incommensurate sines. */
static float wander(float t, float p) {
    return (sinf(t * 0.071f + p) * 0.55f + sinf(t * 0.113f + p * 1.7f) * 0.3f +
            sinf(t * 0.29f + p * 2.3f) * 0.15f);
}

void camera_update(CamState *c, const Settings *s, const Flight *f, float dt, int idle) {
    c->t += dt;
    if (idle) c->since_change += dt;
    c->look_idle += dt;

    /* the automatic rotation */
    if (s->camera_minutes > 0 && c->pending < 0 && c->since_change > s->camera_minutes * 60.f)
        camera_next(c, s);
    /* the cut: fade down, switch, fade up */
    if (c->pending >= 0) {
        c->fade += dt / FADE_TIME;
        if (c->fade >= 1.f) {
            c->fade = 1.f;
            if (c->pending_flip && c->pending == c->kind) {
                c->view = camera_next_view(c);
                c->pending = -1;
                c->look_yaw = c->look_pitch = 0.f;
                plat_log("camera: %s, %s", settings_camera_name(c->kind), camera_view_name(c));
            } else {
                camera_set(c, c->pending);
            }
            c->pending_flip = 0;
        }
    } else if (c->fade > 0.f) {
        c->fade = fmaxf(0.f, c->fade - dt / FADE_TIME);
    }
    /* the gaze eases home after a while */
    if (c->look_idle > LOOK_RETURN) {
        c->look_yaw = approachf(c->look_yaw, 0.f, 3.f, dt);
        c->look_pitch = approachf(c->look_pitch, 0.f, 3.f, dt);
    }

    const Mount *m = camera_mount(c->kind);
    dbasis3 att = flight_attitude(f);
    basis3 attf = dbasis_to_basis(att);

    if (c->kind == CAM_GLOBE && !c->debug) {
        /* Round the middle of the aircraft, in a frame that turns with its
         * heading but stays level, so the horizon does not roll with every
         * bank. Left alone it drifts slowly round. */
        if (c->look_idle > 4.f) c->look_yaw = wrapf(c->look_yaw + dt * DEG2RAD(4.f), -MR_PI, MR_PI);
        vec3 fwd = v3_scale(dv3_to_v3(att.z), -1.f);
        vec3 fh = v3_norm(v3(fwd.x, 0.f, fwd.z));
        vec3 right = v3_norm(v3_cross(fh, v3(0.f, 1.f, 0.f)));
        /* the orbit angle: 0 looks at the nose from ahead-left, a pleasing
         * three-quarter view; pitch up puts the camera above */
        float oy = c->look_yaw + DEG2RAD(-40.f), op = c->look_pitch + DEG2RAD(12.f);
        op = clampf(op, DEG2RAD(-80.f), DEG2RAD(85.f));
        vec3 dir = v3_add(v3_scale(fh, cosf(oy) * cosf(op)), v3_scale(right, sinf(oy) * cosf(op)));
        dir = v3_add(dir, v3(0.f, sinf(op), 0.f));      /* from the aircraft to the camera */
        dvec3 centre = dv3_add(f->pos, dbasis_apply(att, v3_to_dv3(m->pos)));
        c->pos = dv3_add(centre, v3_to_dv3(v3_scale(dir, c->orbit_dist)));
        vec3 back = v3_norm(dir);                          /* the camera's z: away from what it sees */
        vec3 cx = v3_norm(v3_cross(v3(0.f, 1.f, 0.f), back));
        if (v3_len(cx) < 1e-3f) cx = right;
        vec3 cy = v3_cross(back, cx);
        basis3 b = { cx, cy, back };
        c->basis = b;
        c->fov = clampf(DEG2RAD(m->fov) * ((float)s->fov_deg / 62.f) * c->zoom, DEG2RAD(15.f), DEG2RAD(110.f));
        c->near_plane = 1.0f;
        return;
    }

    vec3 mp = m->pos;
    if (c->view & 2) mp.x = -mp.x;       /* the other side */
    float roll_extra = 0.f;
    if (m->on_wing) {
        /* The wing bends as a cantilever: deflection grows with the square
         * of the distance from the root, and the camera tilts with it. */
        float span = (fabsf(mp.x) - 4.5f) / 39.8f;
        if (span > 0.f) {
            mp.y += f->flex * span * span;
            roll_extra = atanf(2.f * f->flex * span / 39.8f) * (mp.x > 0.f ? -1.f : 1.f);
        }
    }

    /* Vibration: the airframe hums with the engines and, in real turbulence,
     * bucks - slowly, a few times a second, as something this heavy does.
     * Mounts far from the centre of mass swing more. */
    float lever = 0.4f + v3_len(mp) / 40.f;
    float chop = f->chop * lever;
    float hum = 0.00018f * f->engine_rpm * lever;
    float t = c->t;
    vec3 jitter = v3(
        (sinf(t * 23.1f + c->shake_phase[0]) + sinf(t * 37.7f + c->shake_phase[1])) * hum,
        (sinf(t * 19.3f + c->shake_phase[2]) + sinf(t * 41.9f + c->shake_phase[3])) * hum,
        0.f);
    jitter.y += (sinf(t * 2.3f + c->shake_phase[4]) * 0.6f + sinf(t * 3.7f + c->shake_phase[5]) * 0.4f) *
                0.035f * chop;
    jitter.x += sinf(t * 1.9f + c->shake_phase[1]) * 0.02f * chop;
    if (m->external) jitter = v3_scale(jitter, 0.f);

    vec3 local = v3_add(mp, jitter);
    dvec3 offset = dbasis_apply(att, v3_to_dv3(local));
    c->pos = dv3_add(f->pos, offset);

    /* Orientation: the mount's own direction, the idle wander, the user's
     * look-around, all relative to the airframe. */
    float yaw, pitch;
    if (m->external) {
        /* An escort flies station: it keeps the aircraft framed, roughly
         * level with the horizon rather than rolling with it. */
        vec3 to = v3_scale(m->pos, -1.f);
        to.y += 2.f;
        yaw = atan2f(to.x, -to.z);
        pitch = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z));
    } else {
        float vy, vp;
        view_dir(m, c->view, &vy, &vp);
        yaw = DEG2RAD(vy);
        pitch = DEG2RAD(vp);
    }
    float dr = DEG2RAD(m->drift);
    yaw += wander(t, 1.3f) * dr + c->look_yaw;
    pitch += wander(t, 4.1f) * dr * 0.45f + c->look_pitch;
    pitch += (sinf(t * 2.9f + c->shake_phase[3]) * 0.5f + sinf(t * 4.3f) * 0.5f) * 0.004f * chop;

    basis3 look = basis_yaw_pitch(yaw, pitch);
    if (roll_extra != 0.f) look = basis_mul(basis_roll(roll_extra), look);
    if (m->external) {
        /* level the escort's view: half the bank, as a cameraman would */
        dbasis3 lvl = att;
        basis3 att_level;
        {
            vec3 fwd = v3_scale(dv3_to_v3(lvl.z), -1.f);
            vec3 fh = v3_norm(v3(fwd.x, 0.f, fwd.z));
            vec3 right = v3_norm(v3_cross(fh, v3(0, 1, 0)));
            vec3 up = v3_cross(right, fh);
            basis3 flat = { right, up, v3_scale(fh, -1.f) };
            att_level.x = v3_norm(v3_lerp(flat.x, attf.x, 0.35f));
            att_level.y = v3_norm(v3_lerp(flat.y, attf.y, 0.35f));
            att_level.z = v3_norm(v3_cross(att_level.x, att_level.y));
            att_level.y = v3_cross(att_level.z, att_level.x);
        }
        c->basis = basis_mul(att_level, look);
    } else {
        c->basis = basis_mul(attf, look);
    }
    c->fov = DEG2RAD(m->fov) * ((float)s->fov_deg / 62.f) * c->zoom;
    c->fov = clampf(c->fov, DEG2RAD(12.f), DEG2RAD(110.f));
    c->near_plane = m->external ? 1.0f : 0.12f;
}
