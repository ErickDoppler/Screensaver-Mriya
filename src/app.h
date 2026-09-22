/* Application shell: window creation, input rules, frame pacing. */
#ifndef MR_APP_H
#define MR_APP_H
#include "settings.h"

typedef enum RunMode {
    MODE_FULLSCREEN,   /* real screensaver run (/s): covers all monitors */
    MODE_PREVIEW,      /* inside the Windows screensaver dialog (/p hwnd) */
    MODE_WINDOW,       /* developer window (/w) */
    MODE_EMBED         /* inside a window another program owns: XScreenSaver */
} RunMode;

typedef struct AppConfig {
    RunMode     mode;
    void       *parent_hwnd;    /* MODE_PREVIEW only */
    unsigned long embed_window; /* MODE_EMBED only */
    Settings    settings;
    int         win_w, win_h;
    const char *dump_path;      /* write a PNG of the frame after frame_limit */
    int         frame_limit;    /* 0 = run until exit */
    int         start_weather;  /* -1 = deal one */
    int         start_camera;   /* -1 = deal one */
    float       start_alt;      /* >0 = start at this altitude */
    float       start_time;     /* seconds to run the simulation ahead first */
    float       look_yaw, look_pitch;  /* degrees, a forced look-around */
    int         look_given;
    int         debug_cam;      /* --view x y z yaw pitch fov: a mount of one's own */
    float       debug_view[6];
    unsigned    seed;
    int         trace;
    float       stick[4];       /* --stick pitch roll from to: hold the stick (tests) */
    int         stick_given;
    int         show_keys;      /* --show-keys: start with the F1 panel up */
} AppConfig;

int app_run(const AppConfig *cfg);
#endif
