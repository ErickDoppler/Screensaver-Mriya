/* User settings. One X-macro table drives defaults, clamping, persistence
 * (registry on Windows) and the command-line parser, so a setting is added in
 * exactly one place. Sliders are stored as 0..100 integers unless noted. */
#ifndef MR_SETTINGS_H
#define MR_SETTINGS_H

/* The weather scenarios, one bit each in scene_mask. Appended, never
 * inserted: the mask is stored as bits, and moving one would silently switch
 * a different scenario off for everybody. */
enum {
    WX_CLEAR = 0,          /* clear morning, a few fair-weather cumulus */
    WX_CUMULUS,            /* a field of scattered cumulus at midday */
    WX_TOWERING,           /* big clouds: cumulus congestus towering up */
    WX_THUNDERSTORM,       /* cumulonimbus, lightning, turbulence, rain shafts */
    WX_HEAVY_RAIN,         /* inside nimbostratus, rain streaming past */
    WX_CLOUD_SEA,          /* above a solid deck in blue sky */
    WX_SUNSET,             /* low golden sun, long shadows in the clouds */
    WX_SUNRISE,            /* dawn over mountains, mist in the valleys */
    WX_MOONLIGHT,          /* clear night, moon, stars, towns lit below */
    WX_NIGHT_STORM,        /* a night thunderstorm lit from inside */
    WX_NIGHT_DECK,         /* a moonlit cloud deck under the stars */
    WX_CIRRUS,             /* high ice veils over the desert */
    WX_SNOWFALL,           /* winter: snow falling, a white land */
    WX_HAZE,               /* hot hazy summer, the horizon lost in it */
    WX_BLUE_HOUR,          /* the last light after sunset */
    WX_DUST,               /* desert dust, the sun a disc in orange murk */
    WX_AURORA,             /* arctic night, the aurora over sea ice */
    WX_STRATOCUMULUS,      /* a broken grey deck with the ground in the gaps */
    WX_ALTOCUMULUS,        /* a mackerel sky high above */
    WX_TROPICAL,           /* tropical towers over a turquoise sea */
    WX_BETWEEN_LAYERS,     /* sandwiched between two decks */
    WX_ALPINE,             /* snow peaks in hard clear air */
    WX_ARCTIC_DAY,         /* ice and a low sun that never climbs */
    WX_DRIZZLE,            /* overcast and grey, light rain under the deck */
    WX_GOLDEN_SEA,         /* late afternoon over the ocean, sun glitter */
    WX_STORMY_SUNSET,      /* a sunset torn through by storm clouds */
    WX_HIMALAYA,           /* over the Himalaya, 8 km peaks in thin blue air */
    WX_COUNT
};
#define WX_ALL ((1 << WX_COUNT) - 1)

/* Where the camera can be bolted on. Same rule: append only. */
enum {
    CAM_NOSE = 0,          /* on top of the nose, looking ahead */
    CAM_BELLY,             /* under the wing root between the engines */
    CAM_SPINE_AFT,         /* on the spine, looking back at the twin tail */
    CAM_FIN_TOP,           /* on top of the right fin, down the whole length */
    CAM_WINGTIP,           /* right wingtip, looking in at the six engines */
    CAM_COCKPIT,           /* the pilot's view over the nose */
    CAM_WINDOW,            /* at a side window, looking back along the wing */
    CAM_CHIN,              /* under the nose, looking back along the belly */
    CAM_ENGINE,            /* behind the inboard engine, under the wing */
    CAM_STAB,              /* on the tailplane tip, looking forward */
    CAM_CHASE,             /* a chase plane behind and above */
    CAM_WINGMAN,           /* a wingman off the left side */
    CAM_GLOBE,             /* free orbit round the aircraft (mouse, wheel) */
    CAM_COUNT
};
#define CAM_ALL ((1 << CAM_COUNT) - 1)

/*  field               key                   default min  max */
#define MR_SETTINGS_INT(X) \
    /* input */ \
    X(mouse_rotation,     "mouse-rotation",     1,      0,   1)    \
    X(exit_on_mouse_move, "exit-on-mouse-move", 1,      0,   1)    \
    X(mouse_sensitivity,  "mouse-sensitivity",  50,     0,   100)  \
    X(exit_on_any_key,    "exit-on-any-key",    0,      0,   1)    \
    /* The pilot may take over. Seconds without input before the autopilot  \
     * takes the aircraft back. */ \
    X(manual_flight,      "manual-flight",      1,      0,   1)    \
    X(autopilot_resume,   "autopilot-resume",   20,     5,   600)  \
    /* a joystick's axes, each turned round if it works the wrong way */ \
    X(joy_inv_roll,       "joy-invert-roll",    0,      0,   1)    \
    X(joy_inv_pitch,      "joy-invert-pitch",   0,      0,   1)    \
    X(joy_inv_throttle,   "joy-invert-throttle", 0,     0,   1)    \
    X(joy_inv_rudder,     "joy-invert-rudder",  0,      0,   1)    \
    /* scenery rotation, minutes; 0 = never change on its own. The cameras  \
     * only rotate once nobody has touched anything for a minute. */ \
    X(weather_minutes,    "weather-minutes",    5,      0,   120)  \
    X(camera_minutes,     "camera-minutes",     5,      0,   60)   \
    X(scene_mask,         "scene-mask",         WX_ALL, 0,   WX_ALL) \
    /* Real time: the daylight and the live weather of a place (its name is  \
     * a string of its own, "rt-place"); coordinates in 1e-4 degrees. */ \
    X(real_time,          "real-time",          0,      0,   1)    \
    X(rt_lat,             "rt-lat",             0,  -900000, 900000) \
    X(rt_lon,             "rt-lon",             0, -1800000, 1800000) \
    X(camera_mask,        "camera-mask",        CAM_ALL, 0,  CAM_ALL) \
    /* The flight plan. Time compression speeds the aircraft along its 1000 \
     * km legs, for anyone who wants to see a turn before the coffee cools. */ \
    X(leg_km,             "leg-km",             1000,   50,  3000) \
    X(turn_km,            "turn-km",            40,     10,  100)  \
    X(altitude_change_km, "altitude-change-km", 300,    20,  1000) \
    X(time_scale,         "time-scale",         1,      1,   20)   \
    /* Take-off weight in tonnes, from nearly empty to the 640 t maximum.   \
     * Lighter climbs faster, flies higher and pitches harder; heavier is   \
     * slower to answer and runs out of wing sooner. */ \
    X(mass_t,             "weight",             450,    300, 640)  \
    /* the picture */ \
    X(fov_deg,            "fov",                62,     30,  100)  \
    X(bloom,              "bloom",              40,     0,   100)  \
    X(lens_effects,       "lens-effects",       1,      0,   1)    \
    X(nav_lights,         "nav-lights",         1,      0,   1)    \
    X(hud,                "hud",                1,      0,   2)    \
    X(units,              "units",              0,      0,   1)    \
    /* power. quality 0 means "measure and settle on its own" */ \
    X(quality,            "quality",            0,      0,   100)  \
    X(target_fps,         "fps",                60,     10,  120)

/* HUD levels */
enum { HUD_OFF = 0, HUD_CAPTIONS = 1, HUD_FULL = 2 };
/* Units */
enum { UNITS_METRIC = 0, UNITS_IMPERIAL = 1 };

typedef struct Settings {
#define X(field, key, def, mn, mx) int field;
    MR_SETTINGS_INT(X)
#undef X
} Settings;

void settings_defaults(Settings *s);
void settings_clamp(Settings *s);
void settings_load(Settings *s);        /* defaults, then platform store */
void settings_save(const Settings *s);
/* Tries to consume argv[*i] (and possibly argv[*i+1]) as a setting override
 * such as "--fov 70", "--bloom=20", "--no-hud", "--scene-mask sunset,aurora".
 * Returns 1 if consumed. */
int settings_parse_arg(Settings *s, int argc, char **argv, int *i);

const char *settings_scene_name(int scene);    /* short key: "sunset" */
const char *settings_scene_title(int scene);   /* shown on screen: "Sunset" */
int  settings_scene_enabled(const Settings *s, int scene);
void settings_set_scene(Settings *s, int scene, int on);
const char *settings_camera_name(int cam);
const char *settings_camera_title(int cam);
int  settings_camera_enabled(const Settings *s, int cam);
void settings_set_camera(Settings *s, int cam, int on);
#endif
