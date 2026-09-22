#include "app.h"
#include "platform.h"
#include "gl_loader.h"
#include "render.h"
#include "flight.h"
#include "weather.h"
#include "camera.h"
#include "hud.h"
#include "trails.h"
#include "realtime.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Seconds without any input before the cameras start rotating again. */
#define CAMERA_IDLE 60.f
/* A camera the user picked is held this long before the rotation resumes. */
#define CAMERA_HOLD 600.f
/* How long a caption stays up, seconds. */
#define CAPTION_TIME 5.f

typedef struct App {
    const AppConfig *cfg;
    Settings    s;
    SDL_Window *win;
    SDL_GLContext gl;
    Renderer    r;
    Flight      fl;
    WeatherState ws;
    CamState    cam;
    int         width, height;
    float       refresh_hz;
    int         use_vsync, swap_n;
    int         running;
    float       elapsed;
    float       last_input;         /* any input at all */
    int         touched;            /* input since the cameras last rotated on their own */
    float       mouse_travel;
    int         frames;
    FlightInput in;
    Trails      trails;
    float       fan_phase;
    /* captions */
    char        caption[256], subcaption[1024];
    float       caption_t;
    int         last_cam, last_weather;
    int         flight_hud;     /* the flight HUD, toggled with H */
    int         help;           /* F1: the key list */
    float       cam_picked;     /* when the user last chose a camera (elapsed s) */
    int         nose_hint;      /* the nose camera's hint is waiting for the caption slot */
    /* real time: the daylight and the live weather of a place */
    int         real, rt_first;
    int         shot;           /* Print Screen: save the frame now on screen */
    /* a joystick, if one is plugged in */
    SDL_Joystick *joy;
    SDL_JoystickID joy_id;
    char        rt_place[256];
    /* adaptive quality, as in The Black Hole */
    int         auto_quality, quality;
    float       frame_ms, quality_hold;
    int         q_w, q_h, quality_saved;
} App;

/* ------------------------------------------------------------------------ */
static SDL_Window *create_window(App *a) {
    const AppConfig *cfg = a->cfg;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Mriya");
    if (cfg->mode == MODE_PREVIEW) {
#ifdef _WIN32
        int w = 0, h = 0;
        void *child = plat_win32_create_preview_child(cfg->parent_hwnd, &w, &h);
        if (!child) { SDL_DestroyProperties(p); return NULL; }
        SDL_SetPointerProperty(p, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, child);
#else
        SDL_DestroyProperties(p);
        return NULL;
#endif
    } else if (cfg->mode == MODE_EMBED) {
#ifdef _WIN32
        SDL_DestroyProperties(p);
        return NULL;
#else
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER, (Sint64)cfg->embed_window);
#endif
    } else if (cfg->mode == MODE_FULLSCREEN) {
        int x = 0, y = 0, w = 1280, h = 720;
#ifdef _WIN32
        plat_win32_virtual_screen(&x, &y, &w, &h);
#else
        SDL_Rect b;
        if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b)) { x = b.x; y = b.y; w = b.w; h = b.h; }
#endif
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, cfg->win_w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, cfg->win_h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    }
    SDL_Window *w = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    return w;
}

static int init_gl(App *a) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    a->win = create_window(a);
    if (!a->win) { plat_log("window: %s", SDL_GetError()); return 0; }
    a->gl = SDL_GL_CreateContext(a->win);
    if (!a->gl) { plat_log("GL context: %s", SDL_GetError()); return 0; }
    SDL_GL_MakeCurrent(a->win, a->gl);
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(a->win));
    a->refresh_hz = (dm && dm->refresh_rate > 0.f) ? dm->refresh_rate : 60.f;
    float ratio = a->refresh_hz / (float)a->s.target_fps;
    int n = (int)floorf(ratio + 0.5f);
    a->use_vsync = n >= 1 && n <= 4 && fabsf(ratio - (float)n) < 0.05f;
    a->swap_n = n;
    if (a->use_vsync && !SDL_GL_SetSwapInterval(1)) a->use_vsync = 0;
    if (!a->use_vsync) SDL_GL_SetSwapInterval(0);
    const char *missing = gl_load_functions();
    if (missing) { plat_log("OpenGL 3.3 function missing: %s", missing); return 0; }
    SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
    return 1;
}

/* ------------------------------------------------------------------------ */
static int passive(const App *a) { return a->cfg->mode == MODE_PREVIEW || a->cfg->mode == MODE_EMBED; }
static void request_exit(App *a) { a->running = 0; }

static void mark_input(App *a) {
    a->last_input = a->elapsed;
    a->touched = 1;
}

static void caption(App *a, const char *big, const char *small) {
    snprintf(a->caption, sizeof a->caption, "%s", big);
    snprintf(a->subcaption, sizeof a->subcaption, "%s", small ? small : "");
    a->caption_t = CAPTION_TIME;
}

/* The digits pick a camera: 1 is the nose, then along the list; 0, - and =
 * reach the last three. */
static int camera_for_key(SDL_Keycode k) {
    if (k >= SDLK_1 && k <= SDLK_9) return (int)(k - SDLK_1);
    if (k == SDLK_0) return 9;
    if (k == SDLK_MINUS) return 10;
    if (k == SDLK_EQUALS) return 11;
    return -1;
}

/* The autopilot on or off at once (Ctrl+A, or a joystick's button 2).
 * Engaged by hand it levels off and stays on until it is released by hand. */
static void toggle_autopilot(App *a) {
    flight_set_autopilot(&a->fl, a->fl.manual);
    if (a->fl.manual)
        caption(a, "Autopilot off", a->joy ? "the stick is yours" : "A/D roll, W/S pitch, arrows rudder, PgUp/PgDn power");
    else
        caption(a, "Autopilot on", a->joy ? "levelling off - Button 2 or Ctrl+A to fly again" : "back on the racetrack");
}

static void handle_key(App *a, const SDL_KeyboardEvent *k) {
    if (k->repeat) return;
    if (k->key == SDLK_ESCAPE) { request_exit(a); return; }
    /* The chords survive "exit on any button": nobody hits them by accident. */
    int ctrl_alt = (k->mod & SDL_KMOD_CTRL) && (k->mod & SDL_KMOD_ALT);
    if (ctrl_alt && k->key == SDLK_S) {
        /* in real time the weather is the real one: fetch it afresh */
        if (a->real) { realtime_refresh(); caption(a, a->rt_place, "fetching the weather now"); }
        else weather_next(&a->ws, &a->s, 1);
        return;
    }
    if (ctrl_alt && k->key == SDLK_C) { camera_next(&a->cam, &a->s); a->cam_picked = a->elapsed; return; }
    /* Ctrl+A: the autopilot, on or off, at once */
    int ctrl_only = (k->mod & SDL_KMOD_CTRL) && !(k->mod & SDL_KMOD_ALT);
    if (ctrl_only && k->key == SDLK_A && a->s.manual_flight) {
        toggle_autopilot(a);
        return;
    }
    /* Print Screen: our own capture of the frame on screen (the system's
     * grab of an OpenGL window can come back stale) */
    if (k->key == SDLK_PRINTSCREEN) { a->shot = 1; return; }
    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) { request_exit(a); return; }

    int cam = camera_for_key(k->key);
    if (cam >= 0 && cam < CAM_COUNT) {
        a->cam_picked = a->elapsed;
        /* the chase key again: the globe round the aircraft, and back */
        if (cam == CAM_CHASE && a->cam.kind == CAM_CHASE) cam = CAM_GLOBE;
        else if (cam == CAM_CHASE && a->cam.kind == CAM_GLOBE) cam = CAM_CHASE;
        /* the same key again moves on through the mount's views */
        if (cam != a->cam.kind) a->cam.pending = cam;
        else if (camera_toggle_view(&a->cam)) {
            /* the name of the view it is moving to */
            CamState next = a->cam;
            next.view = camera_next_view(&a->cam);
            caption(a, settings_camera_title(cam), camera_view_name(&next));
        }
        return;
    }
    switch (k->key) {
    case SDLK_H:
        a->flight_hud = !a->flight_hud;
        break;
    case SDLK_F1:
        a->help = !a->help;
        break;
    case SDLK_G:
        /* the globe camera: round the aircraft */
        if (a->cam.kind != CAM_GLOBE) a->cam.pending = CAM_GLOBE;
        a->cam_picked = a->elapsed;
        break;
    case SDLK_HOME: case SDLK_R:
        a->cam.look_yaw = a->cam.look_pitch = 0.f;
        break;
    default: break;
    }
}

static void handle_mouse_motion(App *a, const SDL_MouseMotionEvent *m) {
    if (a->elapsed < 0.5f) return;       /* the jump when relative mode turns on */
    if (a->s.mouse_rotation) {
        camera_look(&a->cam, m->xrel * 0.0022f, -m->yrel * 0.0022f);
        a->mouse_travel += fabsf(m->xrel) + fabsf(m->yrel);
        if (a->mouse_travel > 40.f) { a->mouse_travel = 0.f; mark_input(a); }
        return;
    }
    if (a->cfg->mode != MODE_FULLSCREEN || !a->s.exit_on_mouse_move) return;
    a->mouse_travel += fabsf(m->xrel) + fabsf(m->yrel);
    float threshold = lerpf(200.f, 4.f, a->s.mouse_sensitivity / 100.f);
    if (a->mouse_travel > threshold) request_exit(a);
}

static void handle_mouse_button(App *a) {
    if (a->cfg->mode != MODE_FULLSCREEN) return;
    if (a->s.mouse_rotation || a->s.exit_on_mouse_move) request_exit(a);
}

static void poll_events(App *a) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: request_exit(a); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!passive(a)) {
                if (!e.key.repeat && e.key.key != SDLK_ESCAPE) mark_input(a);
                handle_key(a, &e.key);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!passive(a)) handle_mouse_motion(a, &e.motion);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!passive(a)) handle_mouse_button(a);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            /* the wheel zooms (the globe camera: moves in and out) */
            if (!passive(a) && a->s.mouse_rotation) {
                float steps = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f);
                camera_wheel(&a->cam, steps);
                mark_input(a);
            }
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
            if (!a->joy && !passive(a)) {
                a->joy = SDL_OpenJoystick(e.jdevice.which);
                if (a->joy) {
                    a->joy_id = e.jdevice.which;
                    const char *name = SDL_GetJoystickName(a->joy);
                    plat_log("joystick: %s, %d axes, %d buttons", name ? name : "?",
                             SDL_GetNumJoystickAxes(a->joy), SDL_GetNumJoystickButtons(a->joy));
                    for (int i = 0; i < SDL_GetNumJoystickAxes(a->joy) && i < 8; ++i)
                        plat_log("joystick: axis %d at %d", i, SDL_GetJoystickAxis(a->joy, i));
                    caption(a, name ? name : "Joystick", "Button 2 or Ctrl+A: autopilot on and off");
                }
            }
            break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            if (a->joy && e.jdevice.which == a->joy_id) {
                SDL_CloseJoystick(a->joy);
                a->joy = NULL;
                plat_log("joystick unplugged");
            }
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            if (a->joy && e.jbutton.which == a->joy_id && !passive(a)) {
                mark_input(a);
                /* button 2: the autopilot, on or off */
                if (e.jbutton.button == 1 && a->s.manual_flight) toggle_autopilot(a);
            }
            break;
        default: break;
        }
    }
}

/* A joystick's response: gentle round the centre for fine control - the
 * first half of the deflection gives only a tenth of the authority - then
 * rising smoothly to all of it at the stops. A little dead zone for a stick
 * that does not quite centre. */
static float stick_curve(float v) {
    float a = fabsf(v);
    if (a < 0.04f) return 0.f;
    a = fminf((a - 0.04f) / 0.96f, 1.f);
    float o;
    if (a <= 0.5f) {
        o = a * 0.2f;
    } else {
        float t = (a - 0.5f) / 0.5f;
        o = 0.1f + 0.9f * (0.3f * t + 0.7f * t * t);
    }
    return v < 0.f ? -o : o;
}

static float joy_axis(SDL_Joystick *j, int i, int invert) {
    float v = (float)SDL_GetJoystickAxis(j, i) / 32767.f;
    v = clampf(v, -1.f, 1.f);
    return invert ? -v : v;
}

/* The joystick: X rolls, Y pitches (back is nose up), a third axis is the
 * throttle lever, a fourth (a twist grip) the rudder. It flies only while
 * the pilot has the aircraft; with the autopilot on it is ignored - a stick
 * at rest is never quite still - until button 2 (or Ctrl+A, or the keys)
 * takes the aircraft back. And while a joystick is plugged in the autopilot
 * never takes over just because nobody is touching anything. */
static void read_joystick(App *a) {
    if (!a->joy || passive(a) || !a->s.manual_flight) return;
    a->in.no_resume = 1;
    if (!a->fl.manual) return;
    int n = SDL_GetNumJoystickAxes(a->joy);
    float roll = n > 0 ? stick_curve(joy_axis(a->joy, 0, a->s.joy_inv_roll)) : 0.f;
    float pitch = n > 1 ? stick_curve(joy_axis(a->joy, 1, a->s.joy_inv_pitch)) : 0.f;
    /* four axes: X, Y, the twist grip, the throttle slider (the usual HID
     * order - Logitech Extreme 3D, Thrustmaster T.16000M); three: the third
     * is the throttle */
    int ax_rud = n > 3 ? 2 : -1, ax_thr = n > 3 ? 3 : (n > 2 ? 2 : -1);
    float yaw = ax_rud >= 0 ? stick_curve(joy_axis(a->joy, ax_rud, a->s.joy_inv_rudder)) : 0.f;
    /* the keys still work alongside, and win */
    if (a->in.roll == 0.f) a->in.roll = roll;
    if (a->in.pitch == 0.f) a->in.pitch = pitch;
    if (a->in.yaw == 0.f) a->in.yaw = yaw;
    if (ax_thr >= 0) {
        /* the lever: pushed forward (the axis's low end) is full power */
        float t = joy_axis(a->joy, ax_thr, a->s.joy_inv_throttle);
        a->in.has_throttle_abs = 1;
        a->in.throttle_abs = clampf(0.5f - 0.5f * t, 0.f, 1.f);
    }
    if (fabsf(roll) + fabsf(pitch) + fabsf(yaw) > 0.02f) mark_input(a);
}

/* The flight controls, read from the held keys each frame.
 *   A / D              roll left / right
 *   W / S              nose down / nose up
 *   arrows left/right  rudder
 *   PgUp / PgDn        power
 * Nothing flies while Ctrl is held: Ctrl+A and the Ctrl+Alt chords share
 * these keys. */
static void read_flight_keys(App *a) {
    memset(&a->in, 0, sizeof a->in);
    if (passive(a) || !a->s.manual_flight) return;
    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) return;
    const bool *k = SDL_GetKeyboardState(NULL);
    if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) return;
    if (k[SDL_SCANCODE_S]) a->in.pitch += 1.f;
    if (k[SDL_SCANCODE_W]) a->in.pitch -= 1.f;
    if (k[SDL_SCANCODE_A]) a->in.roll -= 1.f;
    if (k[SDL_SCANCODE_D]) a->in.roll += 1.f;
    if (k[SDL_SCANCODE_LEFT])  a->in.yaw -= 1.f;
    if (k[SDL_SCANCODE_RIGHT]) a->in.yaw += 1.f;
    if (k[SDL_SCANCODE_PAGEUP])   a->in.throttle += 1.f;
    if (k[SDL_SCANCODE_PAGEDOWN]) a->in.throttle -= 1.f;
    /* a scripted stick, for testing the handling */
    const float *st = a->cfg->stick;
    if (a->cfg->stick_given && a->elapsed >= st[2] && a->elapsed < st[3]) {
        a->in.pitch = st[0];
        a->in.roll = st[1];
    }
    a->in.active = a->in.pitch != 0.f || a->in.roll != 0.f || a->in.yaw != 0.f || a->in.throttle != 0.f;
    if (a->in.active) mark_input(a);
    read_joystick(a);
}

/* When the scenery starts to change, the autopilot heads for an altitude the
 * next scenario suits - and, over mountains coming, well above their peaks,
 * which will not grow until it is. A pilot's altitude is theirs. */
static void aim_for_weather(App *a) {
    if (a->fl.manual) return;
    const Weather *w = &a->ws.next;
    double alt = a->fl.alt_target;
    if (alt < w->alt_lo || alt > w->alt_hi) alt = w->alt_lo + (w->alt_hi - w->alt_lo) * rng_f(&a->ws.rng);
    double peaks = terrain_max_near(&a->ws.t_to, a->fl.pos.x, a->fl.pos.z, 8000.f);
    if (alt < peaks + 800.0) alt = peaks + 800.0;
    flight_aim_altitude(&a->fl, alt);
}

/* Picks where the aircraft is when a scenario opens at once (start-up, or a
 * jump through a cloud bank). Only while the autopilot is flying. */
static void place_for_weather(App *a, int first) {
    const Weather *w = &a->ws.cur;
    /* Out of a jump, whoever is flying: never inside the new ground - a
     * kilometre clear of it. */
    if (a->fl.manual) {
        double g = terrain_max_near(&a->ws.terrain, a->fl.pos.x, a->fl.pos.z, 8000.f);
        if (a->fl.pos.y < g + 1000.0) flight_teleport_altitude(&a->fl, g + 1000.0);
        return;
    }
    double alt = a->fl.pos.y;
    int outside = alt < w->alt_lo || alt > w->alt_hi;
    if (first || outside) alt = w->alt_lo + (w->alt_hi - w->alt_lo) * rng_f(&a->ws.rng);
    double ground = terrain_max_near(&a->ws.terrain, a->fl.pos.x, a->fl.pos.z, 6000.f);
    if (alt < ground + 1000.0) alt = ground + 1000.0;
    if (alt < 1000.0) alt = 1000.0;
    flight_teleport_altitude(&a->fl, alt);
}

static void adapt_quality(App *a, float dt, float budget_ms) {
    if (!a->auto_quality) return;
    if (a->width != a->q_w || a->height != a->q_h) {
        a->q_w = a->width; a->q_h = a->height;
        a->quality_saved = 0;
        a->quality_hold = 1.5f;
        return;
    }
    a->frame_ms = approachf(a->frame_ms, a->r.last_frame_ms, 0.6f, dt);
    a->quality_hold -= dt;
    if (a->quality_hold > 0.f || a->elapsed < 2.0f) return;
    int before = a->quality;
    /* A screensaver runs for hours on a machine nobody is using: it aims to
     * keep the GPU busy for a third to a half of each frame, not all of it -
     * the difference is a quiet fan and a cool card for a picture that is
     * hard to tell apart. */
    if (a->frame_ms > budget_ms * 0.55f)      a->quality -= 6;
    else if (a->frame_ms < budget_ms * 0.3f)  a->quality += 3;
    a->quality = (int)clampf((float)a->quality, 5.f, 100.f);
    a->quality_hold = 2.f;
    if (a->quality == before) {
        if (!a->quality_saved) {
            plat_store_write_int("quality-auto-v2", a->quality);
            a->quality_saved = 1;
            plat_log("quality settled at %d for %dx%d (%.1f ms/frame, budget %.1f)",
                     a->quality, a->width, a->height, a->frame_ms, budget_ms);
        }
        return;
    }
    a->quality_saved = 0;
    a->r.q = render_quality(a->quality);
    render_resize(&a->r, a->width, a->height);
    plat_log("quality -> %d (%.1f ms/frame, budget %.1f)", a->quality, a->frame_ms, budget_ms);
}

/* One step of the world. */
static void simulate(App *a, float dt) {
    float scale = (float)a->s.time_scale;
    double sim_dt = (double)dt * scale;
    const Weather *w = &a->ws.cur;
    float wd = DEG2RAD(w->wind_dir);
    /* the wind strengthens with height, as it does */
    float ws = w->wind_speed * (float)clampd(0.35 + a->fl.pos.y / 9000.0, 0.35, 1.3);
    dvec3 wind = dv3(-sin(wd) * ws, 0.0, cos(wd) * ws);
    float turb = w->turbulence;
    /* flying inside a cloud is bumpy even on a calm day */
    flight_update(&a->fl, &a->s, &a->ws.terrain, &a->in, sim_dt, turb, wind);
    /* Real time: live conditions change the weather gradually as they
     * arrive, the first ones a little faster. */
    if (a->real) {
        Weather lw;
        char desc[160];
        if (realtime_poll(&lw, desc, sizeof desc)) {
            weather_to(&a->ws, &lw, a->rt_first);
            a->rt_first = 0;
            caption(a, a->rt_place, desc);
        }
    }
    /* With a joystick the autopilot never takes over on its own - except
     * when the ground is coming: then it does, and stays on. */
    if (a->joy && a->fl.manual && a->fl.floor_active) {
        flight_set_autopilot(&a->fl, 1);
        caption(a, "Autopilot on", "terrain - Button 2 or Ctrl+A to fly again");
    }
    int wx = weather_update(&a->ws, &a->s, dt, a->fl.pos);
    if (wx & 1) aim_for_weather(a);
    if (wx & 2) place_for_weather(a, 0);
    /* and the sun and the moon where they really are, now */
    if (a->real) {
        Weather *c = &a->ws.cur;
        realtime_sky(a->s.rt_lat / 1e4, a->s.rt_lon / 1e4, time(NULL),
                     &c->sun_elev, &c->sun_azim, &c->moon_elev, &c->moon_azim, &c->moon_phase);
    }
    /* The cameras rotate on their own only when nobody is there: not while
     * the pilot is flying the aircraft, not for ten minutes after the user
     * picked a camera, and not until nothing has been touched for a minute.
     * The moment all that holds, the rotation picks up with a fresh camera. */
    int idle = a->elapsed - a->last_input >= CAMERA_IDLE &&
               a->elapsed - a->cam_picked >= CAMERA_HOLD &&
               !a->fl.manual;
    if (idle && a->touched) {
        a->touched = 0;
        if (a->s.camera_minutes > 0) camera_next(&a->cam, &a->s);
    }
    camera_update(&a->cam, &a->s, &a->fl, dt, idle);

    /* the fans turn with the engines; the contrails are laid behind them */
    a->fan_phase = fmodf(a->fan_phase + dt * 2.f * MR_PI * (0.9f + 1.6f * a->fl.engine_rpm), 2.f * MR_PI * 64.f);
    trails_update(&a->trails, a->elapsed, dt, a->fl.pos, flight_attitude(&a->fl),
                  dv3(a->ws.wind_offset_x, 0.0, a->ws.wind_offset_z),
                  trails_condensation(a->fl.pos.y, a->ws.cur.humidity) * (1.f - a->ws.passage));

    /* captions for whatever just changed */
    /* Named when it has arrived - not when it starts: a change takes a
     * minute, and the name and the "over ..." must be what is on screen. */
    if (a->ws.arrived != a->last_weather && a->ws.passage < 0.6f && !a->real) {
        a->last_weather = a->ws.arrived;
        char sub[128];
        snprintf(sub, sizeof sub, "over %s", terrain_biome_name(a->ws.cur.biome));
        caption(a, settings_scene_title(a->ws.kind), sub);
    }
    if (a->cam.kind != a->last_cam) {
        a->last_cam = a->cam.kind;
        if (a->cam.kind == CAM_NOSE) a->nose_hint = 1;
        else if (a->caption_t < CAPTION_TIME - 1.f) {
            char sub[64];
            snprintf(sub, sizeof sub, "camera %d of %d", a->cam.kind + 1, CAM_COUNT);
            caption(a, settings_camera_title(a->cam.kind), sub);
        }
    }
    /* on the nose camera, tell the user what they can do - once whatever
     * caption is up has had its moment */
    if (a->cam.kind != CAM_NOSE) a->nose_hint = 0;
    if (a->nose_hint && !passive(a) && (a->caption_t < CAPTION_TIME - 2.5f || a->caption_t <= 0.f)) {
        a->nose_hint = 0;
        caption(a, settings_camera_title(CAM_NOSE),
                "Press \"H\" to toggle HUD, \"WASD\" to fly, \"F1\" for all the keys");
    }
    a->caption_t -= dt;
}

static void draw(App *a, float dt) {
    Frame f;
    memset(&f, 0, sizeof f);
    f.cam_pos = a->cam.pos;
    f.cam_basis = a->cam.basis;
    f.fov = a->cam.fov;
    f.near_plane = a->cam.near_plane;
    const Mount *m = camera_mount(a->cam.kind);
    f.cam_external = m->external;
    f.cam_on_airframe = !m->external;
    f.ac_pos = a->fl.pos;
    f.ac_basis = dbasis_to_basis(flight_attitude(&a->fl));
    double cg = cos(a->fl.gamma);
    f.ac_vel = v3((float)(a->fl.ground_speed * sin(a->fl.track) * 1.0), (float)a->fl.vs,
                  (float)(-a->fl.ground_speed * cos(a->fl.track)));
    (void)cg;
    f.flex = a->fl.flex;
    f.agl = (float)(a->cam.pos.y - fmax(a->fl.ground, 0.0));
    f.wx = &a->ws.cur;
    f.ws = &a->ws;
    f.tp = &a->ws.terrain;
    f.time = a->elapsed;
    f.dt = dt;
    f.passage = a->ws.passage;
    f.fade = a->cam.fade;
    f.nav_lights = a->s.nav_lights;
    f.lens = a->s.lens_effects;
    f.bloom = a->s.bloom / 100.f;
    f.fan_angle = a->fan_phase;
    f.surf = v3(a->fl.ail, a->fl.elev, a->fl.rud);
    f.fan_blur = clampf(a->fl.engine_rpm * 1.1f - 0.1f, 0.f, 1.f);
    f.haze = 0.35f + 0.65f * (float)a->fl.throttle;
    f.trails = &a->trails;
    f.wind_off = dv3(a->ws.wind_offset_x, 0.0, a->ws.wind_offset_z);
    render_frame(&a->r, &f);

    if ((a->s.hud > HUD_OFF || a->flight_hud || a->help) && !passive(a)) {
        HudInfo h;
        memset(&h, 0, sizeof h);
        h.fl = &a->fl;
        h.width = a->width;
        h.height = a->height;
        h.show_flight = a->flight_hud;          /* on every camera until H again */
        h.units = a->s.units;
        h.caption = a->caption_t > 0.f ? a->caption : NULL;
        h.subcaption = a->subcaption[0] ? a->subcaption : NULL;
        h.caption_alpha = clampf(a->caption_t / 1.2f, 0.f, 1.f) * clampf((CAPTION_TIME - a->caption_t) / 0.4f, 0.f, 1.f);
        h.fade = a->cam.fade;
        h.cam_basis = a->cam.basis;
        h.fov = a->cam.fov;
        h.passage = a->ws.passage;
        h.autopilot_resume = a->s.autopilot_resume;
        h.show_help = a->help;
        h.joystick = a->joy != NULL;
        hud_draw(&h);
    }
}

/* ------------------------------------------------------------------------ */
int app_run(const AppConfig *cfg) {
    App a;
    memset(&a, 0, sizeof a);
    a.cfg = cfg;
    a.s = cfg->settings;
    settings_clamp(&a.s);
    if (cfg->mode == MODE_PREVIEW) {
        if (a.s.target_fps > 24) a.s.target_fps = 24;
        a.s.quality = 20;
        a.s.hud = HUD_OFF;
    }
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (cfg->mode == MODE_EMBED) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
        SDL_SetHint(SDL_HINT_VIDEO_X11_EXTERNAL_WINDOW_INPUT, "0");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        plat_log("SDL_Init: %s", SDL_GetError());
        return 2;
    }
    /* the joystick is a nicety: without it the screensaver runs all the same */
    if (!passive(&a) && !SDL_InitSubSystem(SDL_INIT_JOYSTICK))
        plat_log("no joystick support: %s", SDL_GetError());
    int code = 0;
    /* a log of every real run, so a failure on someone's machine can be told */
    char logpath[512] = "";
    if (!passive(&a)) plat_log_default(logpath, sizeof logpath);
    plat_log("Mriya %s, mode %d", MR_VERSION, (int)cfg->mode);
    if (!init_gl(&a)) { code = 3; goto done; }
    if (cfg->mode == MODE_EMBED && a.width < 640) { a.s.quality = 20; a.s.hud = HUD_OFF; }

    a.auto_quality = a.s.quality == 0 && cfg->mode != MODE_PREVIEW && !cfg->dump_path;
    a.quality = a.s.quality > 0 ? a.s.quality : 60;
    if (a.auto_quality) {
        int stored = 0;
        if (plat_store_read_int("quality-auto-v2", &stored) && stored >= 5 && stored <= 100) a.quality = stored;
    }
    plat_log("start: OpenGL up, renderer next");
    if (!render_init(&a.r, a.quality)) { code = 4; goto done; }
    plat_log("start: renderer ready");
    a.r.q = render_quality(a.quality);
    render_resize(&a.r, a.width, a.height);
    if (!hud_init()) { code = 4; goto done; }
    a.q_w = a.width;
    a.q_h = a.height;
    a.frame_ms = 1000.f / (float)a.s.target_fps;

    unsigned seed = cfg->seed ? cfg->seed : (unsigned)SDL_GetPerformanceCounter();
    trails_reset(&a.trails);
    weather_init(&a.ws, &a.s, seed, cfg->start_weather);
    flight_init(&a.fl, &a.s, seed * 747796405u + 1u, 5000.0, (seed >> 3) & 1);
    place_for_weather(&a, 1);
    if (cfg->start_alt > 0.f) flight_teleport_altitude(&a.fl, cfg->start_alt);
    camera_init(&a.cam, &a.s, seed * 2891336453u + 7u, cfg->start_camera);
    if (cfg->debug_cam) {
        camera_debug(&a.cam, v3(cfg->debug_view[0], cfg->debug_view[1], cfg->debug_view[2]),
                     cfg->debug_view[3], cfg->debug_view[4], cfg->debug_view[5]);
        a.s.camera_minutes = 0;
    }
    if (cfg->look_given) {
        a.cam.look_yaw = DEG2RAD(cfg->look_yaw);
        a.cam.look_pitch = DEG2RAD(cfg->look_pitch);
        a.cam.look_idle = -1e9f;
    }
    a.last_weather = a.ws.arrived;
    a.last_cam = a.cam.kind;
    a.flight_hud = a.s.hud == HUD_FULL;
    a.help = cfg->show_keys;
    a.cam_picked = -1e9f;
    a.nose_hint = a.cam.kind == CAM_NOSE;
    /* Real time needs a place; the automatic rotation stops, the weather is
     * the real one. Not in the little preview monitor. */
    if (a.s.real_time && (a.s.rt_lat || a.s.rt_lon) && !passive(&a)) {
        a.real = 1;
        a.rt_first = 1;
        if (!plat_store_read_str("rt-place", a.rt_place, sizeof a.rt_place) || !a.rt_place[0])
            snprintf(a.rt_place, sizeof a.rt_place, "%.2f, %.2f", a.s.rt_lat / 1e4, a.s.rt_lon / 1e4);
        a.s.weather_minutes = 0;
        realtime_start(a.s.rt_lat / 1e4, a.s.rt_lon / 1e4);
        Weather *c = &a.ws.cur;
        realtime_sky(a.s.rt_lat / 1e4, a.s.rt_lon / 1e4, time(NULL),
                     &c->sun_elev, &c->sun_azim, &c->moon_elev, &c->moon_azim, &c->moon_phase);
        plat_log("real time: %s", a.rt_place);
    }
    {
        char sub[128];
        snprintf(sub, sizeof sub, "over %s", terrain_biome_name(a.ws.cur.biome));
        if (a.real) caption(&a, a.rt_place, "real time - the weather is on its way");
        else caption(&a, settings_scene_title(a.ws.kind), sub);
    }
    /* run the world ahead without drawing, for testing a later moment */
    for (float t = 0.f; t < cfg->start_time; t += 0.05f) {
        a.elapsed += 0.05f;
        simulate(&a, 0.05f);
    }
    a.last_input = a.elapsed;

    if (cfg->mode == MODE_FULLSCREEN) {
        SDL_HideCursor();
        if (a.s.mouse_rotation) SDL_SetWindowRelativeMouseMode(a.win, true);
        SDL_RaiseWindow(a.win);
    }
    plat_log("start: mode=%d %dx%d fps=%d (display %.0f Hz, %s) weather=%s camera=%s alt=%.0f quality=%d%s",
             cfg->mode, a.width, a.height, a.s.target_fps, a.refresh_hz, a.use_vsync ? "vsync" : "paced",
             settings_scene_name(a.ws.kind), settings_camera_name(a.cam.kind), a.fl.pos.y, a.quality,
             a.auto_quality ? " (auto)" : "");

    const Uint64 frame_ns = 1000000000ull / (Uint64)a.s.target_fps;
    const float budget_ms = 1000.f / (float)a.s.target_fps;
    Uint64 last = SDL_GetTicksNS();
    Uint64 next_deadline = last + frame_ns;
    a.running = 1;
    while (a.running) {
        Uint64 now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (cfg->dump_path) dt = 1.f / 60.f;      /* reproducible */
        a.elapsed += dt;

        poll_events(&a);
#ifdef _WIN32
        if (cfg->mode == MODE_PREVIEW && !plat_win32_window_alive(cfg->parent_hwnd)) break;
#endif
        if (!a.running) break;
        a.mouse_travel *= expf(-dt / 0.6f);
        read_flight_keys(&a);
        simulate(&a, dt);
        render_resize(&a.r, a.width, a.height);
        draw(&a, dt);
        adapt_quality(&a, dt, budget_ms);
        a.frames++;
        if (a.frames == 1 || a.frames == 2 || a.frames == 10 || a.frames == 120)
            plat_log("frame %d drawn (gpu %.1f ms, quality %d)", a.frames, a.r.last_frame_ms, a.quality);
        if (cfg->trace && a.frames % 15 == 0)
            plat_log("t=%6.1f pos=(%.0f, %.0f, %.0f) hdg=%5.1f v=%5.1f vs=%+5.1f pitch=%+5.1f bank=%+5.1f n=%.2f thr=%.2f "
                     "ap=%d leg=%.0fkm tgt=%.0f floor=%.0f agl=%.0f wx=%s cam=%s q=%d gpu=%.1fms",
                     a.elapsed, a.fl.pos.x, a.fl.pos.y, a.fl.pos.z,
                     wrapd(flight_heading(&a.fl) * 180.0 / MR_PI_D, 0.0, 360.0), a.fl.speed, a.fl.vs,
                     flight_pitch(&a.fl) * 180.0 / MR_PI_D, a.fl.bank * 180.0 / MR_PI_D, a.fl.n, a.fl.throttle, !a.fl.manual,
                     a.fl.leg_flown / 1000.0, a.fl.alt_target, a.fl.floor_alt, a.fl.pos.y - a.fl.ground,
                     settings_scene_name(a.ws.kind), settings_camera_name(a.cam.kind), a.quality,
                     a.r.last_frame_ms);
        if (cfg->dump_path && cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) {
            if (render_dump_png(&a.r, cfg->dump_path)) plat_log("wrote %s", cfg->dump_path);
            else { plat_log("failed to write %s", cfg->dump_path); code = 5; }
        }
        if (a.shot) {
            a.shot = 0;
            int w, h;
            unsigned char *px = render_read_rgba(&a.r, &w, &h);
            char path[1024];
            if (px && plat_screenshot_path(path, sizeof path) && render_write_png(path, px, w, h)) {
                plat_clipboard_image(px, w, h);
                const char *name = strrchr(path, '\\');
                if (!name) name = strrchr(path, '/');
                caption(&a, "Screenshot saved", name ? name + 1 : path);
                plat_log("screenshot: %s", path);
            }
            free(px);
        }
        SDL_GL_SwapWindow(a.win);
        if (cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) break;
        if (a.use_vsync && a.swap_n == 1) continue;
        now = SDL_GetTicksNS();
        if (next_deadline > now + 200000ull) SDL_DelayPrecise(next_deadline - now);
        now = SDL_GetTicksNS();
        next_deadline += frame_ns;
        if (next_deadline < now) next_deadline = now + frame_ns;
    }
    plat_log("stop: %d frames in %.2f s (%.1f fps avg), quality=%d, %.1f ms/frame",
             a.frames, a.elapsed, a.elapsed > 0.f ? a.frames / a.elapsed : 0.f, a.quality, a.frame_ms);
done:
    /* started for real, and it could not: say why, rather than vanish */
    if (code >= 3 && code <= 4 && !passive(&a) && !cfg->dump_path) {
        char msg[1024];
        const char *why = render_failure();
        snprintf(msg, sizeof msg,
                 "Mriya could not start its 3D graphics.\n\n%s\n\n"
                 "It needs OpenGL 3.3. On a laptop with two graphics chips, choosing the "
                 "high-performance GPU for Mriya.scr in Windows' graphics settings may help.\n\n"
                 "Details are in:\n%s",
                 why ? why : (code == 3 ? "No OpenGL 3.3 context could be created." : "Setting up the renderer failed."),
                 logpath[0] ? logpath : "(no log)");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mriya", msg, a.win);
    }
    realtime_stop();
    if (a.joy) SDL_CloseJoystick(a.joy);
    hud_shutdown();
    render_shutdown(&a.r);
    if (a.gl) SDL_GL_DestroyContext(a.gl);
    if (a.win) SDL_DestroyWindow(a.win);
    SDL_Quit();
    return code;
}
