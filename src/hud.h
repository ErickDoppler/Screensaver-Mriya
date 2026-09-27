/* The on-screen display: captions when the scenery or the camera changes,
 * and the flight HUD, which H turns on and off for every camera. */
#ifndef MR_HUD_H
#define MR_HUD_H
#include "mathx.h"
#include "flight.h"
#include "settings.h"

typedef struct HudInfo {
    const Flight *fl;
    int   width, height;
    int   show_flight;          /* the full flight HUD */
    int   units;                /* UNITS_* */
    const char *caption;        /* big line, fading */
    const char *subcaption;
    float caption_alpha;
    float fade;                 /* 1 = black: hide everything */
    basis3 cam_basis;           /* for the horizon and flight path marker */
    float fov;
    float passage;
    int   autopilot_resume;     /* seconds, for the countdown */
    int   show_help;            /* F1: every key, in a panel */
    int   joystick;             /* a joystick is plugged in */
} HudInfo;

int  hud_init(void);
void hud_draw(const HudInfo *h);
void hud_shutdown(void);

/* The same text and shapes, for anything else that needs them - the menu in
 * the headset draws itself with these, into whatever framebuffer is bound.
 * Between begin and flush, coordinates are pixels of a `w` x `h` page, y down.
 */
void hud_ui_begin(int w, int h, float scale);
void hud_ui_rect(float x0, float y0, float x1, float y1, const float col[4]);
void hud_ui_frame(float x0, float y0, float x1, float y1, float lw, const float col[4]);
void hud_ui_text(float x, float y, const char *s, float scale, int align, const float col[4], int shadow);
float hud_ui_text_width(const char *s, float scale);
void hud_ui_flush(void);
#endif
