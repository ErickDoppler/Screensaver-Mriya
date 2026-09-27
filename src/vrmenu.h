/* The menu in the headset: a panel hanging in the air, pointed at with a
 * controller. B or Y puts it up and takes it down.
 *
 * It draws itself into a texture with the HUD's own text engine, and the
 * renderer hangs that texture on a quad in the world, the same for both eyes.
 */
#ifndef MR_VRMENU_H
#define MR_VRMENU_H
#include "mathx.h"
#include "settings.h"

typedef struct VrMenu {
    int      open;
    int      page;              /* 0 top, 1 cameras, 2 scenery, 3 settings */
    int      hover;             /* the item the pointer is on, -1 for none */
    int      scroll;            /* first item shown, for the long lists */
    float    drag;              /* a slider being dragged, 0..1, or < 0 */
    unsigned tex;               /* what the panel shows */
    unsigned fbo;
    int      w, h;              /* the page, in its own pixels */
    /* where the panel hangs, in the camera's frame */
    vec3     pos;
    basis3   basis;
    float    width_m, height_m;
} VrMenu;

/* What the menu asks the app to do, returned by vrmenu_click. */
enum {
    VRMENU_NONE = 0,
    VRMENU_TOGGLE_HUD,
    VRMENU_CAMERA,              /* value: which camera */
    VRMENU_SCENE,               /* value: which scenario */
    VRMENU_QUALITY,             /* value: 0..100 */
    VRMENU_AUTOPILOT,           /* value: seconds */
    VRMENU_LENS,                /* value: which lens */
    VRMENU_EXIT                 /* leave the screensaver */
};

/* The lens types the settings page offers, in order. */
enum { LENS_PORTRAIT, LENS_NORMAL, LENS_TELE, LENS_WIDE, LENS_FISHEYE, LENS_COUNT };
const char *vrmenu_lens_name(int lens);
/* How much of the view a lens shows: 1 is the headset's own. */
float vrmenu_lens_zoom(int lens);

int  vrmenu_init(VrMenu *m);
void vrmenu_shutdown(VrMenu *m);
void vrmenu_open(VrMenu *m, int open);
/* Aims the pointer: the ray in the camera's frame. Returns 1 if it is on the
 * panel, and puts where in `u`, `v` (0..1). */
int  vrmenu_aim(VrMenu *m, vec3 from, vec3 dir, float *u, float *v);
/* The trigger, on the item under the pointer. `value` carries what came with
 * it (a camera, a scenario, a slider's new setting). */
int  vrmenu_click(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                  float u, float v, int *value);
/* Just pointing: lights up the row under the pointer. */
int  vrmenu_hover(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                  float u, float v, int *value);
/* Slider dragging while the trigger is held. */
int  vrmenu_drag(VrMenu *m, float u, float v, int *value);
void vrmenu_release(VrMenu *m);
/* Redraws the panel's texture. */
void vrmenu_paint(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens);
#endif
