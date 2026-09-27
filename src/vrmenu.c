/* The menu panel in the headset. See vrmenu.h.
 *
 * The page is laid out in its own pixels and drawn into a texture with the
 * HUD's text engine; the renderer hangs it on a quad in the world. The
 * pointer is a ray from a controller, turned into a position on the page, so
 * hit testing is the same arithmetic as the layout.
 */
#include "vrmenu.h"
#include "hud.h"
#include "render.h"
#include "gl_loader.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define PAGE_W 1024
#define PAGE_H 768
#define ROW_H  54.f
#define TOP    120.f
#define PAD    40.f

static const char *const lens_names[LENS_COUNT] = {
    "Portrait", "Normal", "Telescopic", "Wide", "Fisheye"
};
/* Each lens as a magnification of the virtual camera. */
static const float lens_zoom[LENS_COUNT] = { 1.6f, 1.0f, 3.0f, 0.75f, 0.6f };

const char *vrmenu_lens_name(int l) { return (l >= 0 && l < LENS_COUNT) ? lens_names[l] : "?"; }
float vrmenu_lens_zoom(int l) { return (l >= 0 && l < LENS_COUNT) ? lens_zoom[l] : 1.f; }

int vrmenu_init(VrMenu *m) {
    memset(m, 0, sizeof *m);
    m->w = PAGE_W; m->h = PAGE_H;
    m->hover = -1;
    m->drag = -1.f;
    m->width_m = 1.15f; m->height_m = 1.15f * (float)PAGE_H / (float)PAGE_W;
    glGenTextures(1, &m->tex);
    glBindTexture(GL_TEXTURE_2D, m->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PAGE_W, PAGE_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m->tex, 0);
    int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    plat_log("vr: menu panel %dx%d, texture %u, %s", PAGE_W, PAGE_H, m->tex, ok ? "ready" : "incomplete");
    return ok;
}

void vrmenu_shutdown(VrMenu *m) {
    if (m->tex) glDeleteTextures(1, &m->tex);
    if (m->fbo) glDeleteFramebuffers(1, &m->fbo);
    m->tex = m->fbo = 0;
}

void vrmenu_open(VrMenu *m, int open) {
    m->open = open;
    /* Put up again, it belongs in front of whoever put it up - not where it
     * was left the last time. The placing waits for a frame with poses in
     * it, since the runtime does not always hand them over. */
    if (open) { m->place_pending = 1; m->page = 0; m->scroll = 0; }
    else { m->page = 0; m->scroll = 0; m->drag = -1.f; m->hover = -1; }
}

/* ---- the layout ---------------------------------------------------------
 * Every page is a list of rows, and a row's job is either to go somewhere, to
 * pick something, or to be a slider. The same function lays them out and
 * answers what is under the pointer, so the two can never disagree. */
typedef struct Row {
    const char *label;
    int kind;        /* 0 plain, 1 submenu, 2 pick, 3 slider, 4 back, 5 page */
    int action, value;
    int checked;
    float frac;      /* sliders: where the handle sits */
    char right[32];  /* what it says on the right */
} Row;

static int rows_of(const VrMenu *m, const Settings *s, int hud_on, int camera, int scene,
                   int lens, Row *out, int cap) {
    int n = 0;
    if (m->page == 0) {
        if (n < cap) { out[n] = (Row){ "HUD", 2, VRMENU_TOGGLE_HUD, 0, hud_on, 0.f, "" }; n++; }
        if (n < cap) { out[n] = (Row){ "Camera", 1, 0, 1, 0, 0.f, "" };
                       snprintf(out[n].right, sizeof out[n].right, "%s", settings_camera_title(camera)); n++; }
        if (n < cap) { out[n] = (Row){ "Environment", 1, 0, 2, 0, 0.f, "" };
                       snprintf(out[n].right, sizeof out[n].right, "%s", settings_scene_title(scene)); n++; }
        if (n < cap) { out[n] = (Row){ "Settings", 1, 0, 3, 0, 0.f, "" }; n++; }
        if (n < cap) { out[n] = (Row){ "Exit", 2, VRMENU_EXIT, 0, 0, 0.f, "" }; n++; }
    } else if (m->page == 1) {
        if (n < cap) { out[n] = (Row){ "Back", 4, 0, 0, 0, 0.f, "" }; n++; }
        for (int i = 0; i < CAM_COUNT && n < cap; ++i) {
            out[n] = (Row){ settings_camera_title(i), 2, VRMENU_CAMERA, i, i == camera, 0.f, "" };
            n++;
        }
    } else if (m->page == 2) {
        if (n < cap) { out[n] = (Row){ "Back", 4, 0, 0, 0, 0.f, "" }; n++; }
        for (int i = 0; i < WX_COUNT && n < cap; ++i) {
            out[n] = (Row){ settings_scene_title(i), 2, VRMENU_SCENE, i, i == scene, 0.f, "" };
            n++;
        }
    } else {
        if (n < cap) { out[n] = (Row){ "Back", 4, 0, 0, 0, 0.f, "" }; n++; }
        if (n < cap) {
            out[n] = (Row){ "Quality", 3, VRMENU_QUALITY, 0, 0, s->quality / 100.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%d", s->quality);
            n++;
        }
        if (n < cap) {
            out[n] = (Row){ "Autopilot takes over after", 3, VRMENU_AUTOPILOT, 0, 0,
                            (s->autopilot_resume - 5) / 595.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%d s", s->autopilot_resume);
            n++;
        }
        /* the lens is the headset's own: on a monitor the field of view is
         * the one in the settings dialog, and this would do nothing */
        if (n < cap && !m->on_screen) {
            out[n] = (Row){ "Lens", 2, VRMENU_LENS, (lens + 1) % LENS_COUNT, 0, 0.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%s", vrmenu_lens_name(lens));
            n++;
        }
        int have_place = s->rt_lat || s->rt_lon;
        if (n < cap) {
            out[n] = (Row){ "Real daylight", 2, VRMENU_RT_DAYLIGHT, 0, s->rt_daylight, 0.f, "" };
            if (!have_place && s->rt_daylight)
                snprintf(out[n].right, sizeof out[n].right, "no city set");
            n++;
        }
        if (n < cap) {
            out[n] = (Row){ "Real weather", 2, VRMENU_RT_WEATHER, 0, s->rt_weather, 0.f, "" };
            if (!have_place)
                snprintf(out[n].right, sizeof out[n].right, "no city set");
            n++;
        }
        if (n < cap && !s->rt_daylight) {
            /* the hour is the user's own: a slider, shown as a clock */
            out[n] = (Row){ "Time of day", 3, VRMENU_TIME_OF_DAY, 0, 0, s->time_of_day / 1439.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%02d:%02d",
                     s->time_of_day / 60, s->time_of_day % 60);
            n++;
        }
    }
    return n;
}

/* How many rows fit, and where each one sits on the page. */
static int rows_visible(void) { return (int)((PAGE_H - TOP - PAD) / ROW_H) - 1; }
static float row_y(int i) { return TOP + (float)i * ROW_H; }

/* The list's own rows, then the page turners when there are more than fit. */
static int paged(const VrMenu *m, Row *in, int n, Row *out, int cap) {
    int vis = rows_visible(), k = 0;
    if (n <= vis) {
        for (int i = 0; i < n && k < cap; ++i) out[k++] = in[i];
        return k;
    }
    int first = m->scroll;
    if (first > n - vis) first = n - vis;
    if (first < 0) first = 0;
    for (int i = first; i < first + vis && i < n && k < cap; ++i) out[k++] = in[i];
    if (k < cap) {
        Row r = { first > 0 ? "Page up" : "", 5, 0, -1, 0, 0.f, "" };
        if (first > 0) out[k++] = r;
    }
    if (k < cap && first + vis < n) {
        Row r = { "Page down", 5, 0, 1, 0, 0.f, "" };
        snprintf(r.right, sizeof r.right, "%d more", n - first - vis);
        out[k++] = r;
    }
    return k;
}

/* What a slider's position means, for each of them. */
static int slider_value(int action, float f) {
    if (action == VRMENU_QUALITY) return (int)(f * 100.f + 0.5f);
    if (action == VRMENU_AUTOPILOT) return (int)(5.f + f * 595.f);
    if (action == VRMENU_TIME_OF_DAY) return (int)(f * 1439.f + 0.5f);
    return 0;
}

static int row_at(const VrMenu *m, float u, float v, int count) {
    (void)m;
    float x = u * PAGE_W, y = v * PAGE_H;
    if (x < PAD || x > PAGE_W - PAD) return -1;
    for (int i = 0; i < count; ++i) {
        float ry = row_y(i);
        if (y >= ry && y < ry + ROW_H - 6.f) return i;
    }
    return -1;
}

/* ---- pointing ------------------------------------------------------------ */
int vrmenu_aim(VrMenu *m, vec3 from, vec3 dir, float *u, float *v) {
    if (!m->open) return 0;
    /* the panel's plane: through m->pos, facing -z of its own basis */
    vec3 n = m->basis.z;
    float denom = v3_dot(n, dir);
    if (fabsf(denom) < 1e-4f) return 0;
    float t = v3_dot(n, v3_sub(m->pos, from)) / denom;
    if (t < 0.05f || t > 12.f) return 0;
    vec3 hit = v3_add(from, v3_scale(dir, t));
    vec3 d = v3_sub(hit, m->pos);
    float x = v3_dot(d, m->basis.x) / m->width_m + 0.5f;
    float y = 0.5f - v3_dot(d, m->basis.y) / m->height_m;
    if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) return 0;
    *u = x; *v = y;
    return 1;
}

/* Just pointing: which row is under it, nothing acted upon. */
int vrmenu_hover(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                 float u, float v, int *value) {
    Row all[64], rows[32];
    int n = paged(m, all, rows_of(m, s, hud_on, camera, scene, lens, all, 64), rows, 32);
    (void)value;
    m->hover = row_at(m, u, v, n);
    return VRMENU_NONE;
}

/* The rows are built from the real settings, so a slider can be read off
 * wherever the pointer is. */
int vrmenu_click(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                 float u, float v, int *value) {
    Row all[64], rows[32];
    int n = paged(m, all, rows_of(m, s, hud_on, camera, scene, lens, all, 64), rows, 32);
    int idx = row_at(m, u, v, n);
    m->hover = idx;
    if (idx < 0) return VRMENU_NONE;
    Row *r = &rows[idx];
    if (r->kind == 5) { m->scroll += r->value * rows_visible(); return VRMENU_NONE; }
    if (r->kind == 4) { m->page = 0; m->scroll = 0; return VRMENU_NONE; }
    if (r->kind == 1) { m->page = r->value; m->scroll = 0; return VRMENU_NONE; }
    if (r->kind == 3) {
        m->drag = (float)r->action;          /* what is being dragged, not where */
        float x0 = PAGE_W * 0.52f, x1 = PAGE_W - PAD - 90.f;
        float f = clampf((u * PAGE_W - x0) / (x1 - x0), 0.f, 1.f);
        *value = slider_value(r->action, f);
        return r->action;
    }
    *value = r->value;
    return r->action;
}

int vrmenu_drag(VrMenu *m, float u, float v, int *value) {
    (void)v;
    if (m->drag < 0.f) return VRMENU_NONE;
    float x0 = PAGE_W * 0.52f, x1 = PAGE_W - PAD - 90.f;
    float f = clampf((u * PAGE_W - x0) / (x1 - x0), 0.f, 1.f);
    int what = (int)m->drag;
    *value = slider_value(what, f);
    return what;
}

void vrmenu_release(VrMenu *m) { m->drag = -1.f; }

/* ---- drawing ------------------------------------------------------------- */
/* The rows of the page that is showing, with the scroll kept in range. */
static int page_rows(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                     Row *rows, int cap, int *total_out, int *vis_out) {
    Row all[64];
    int total = rows_of(m, s, hud_on, camera, scene, lens, all, 64);
    int vis = rows_visible();
    if (m->scroll > total - vis) m->scroll = total - vis;
    if (m->scroll < 0) m->scroll = 0;
    if (total_out) *total_out = total;
    if (vis_out) *vis_out = vis;
    return paged(m, all, total, rows, cap);
}

/* The page itself, in its own pixels moved and scaled onto whatever is being
 * painted: its texture for the headset, the window for the screensaver. The
 * drawing is the same either way, so the two can never drift apart. */
static void draw_page(const VrMenu *m, const Row *rows, int n, int total, int vis,
                      float ox, float oy, float sc) {
#define PX(x) (ox + (x) * sc)
#define PY(y) (oy + (y) * sc)
    const float bg[4]    = { 0.03f, 0.05f, 0.07f, 0.88f };
    const float edge[4]  = { 0.45f, 0.85f, 0.6f,  0.85f };
    const float head[4]  = { 1.0f,  0.82f, 0.25f, 1.f };
    const float txt[4]   = { 0.88f, 0.92f, 0.95f, 1.f };
    const float dim[4]   = { 0.55f, 0.62f, 0.68f, 1.f };
    const float sel[4]   = { 0.12f, 0.35f, 0.25f, 0.9f };
    const float bar[4]   = { 0.25f, 0.45f, 0.35f, 0.9f };
    const float barbg[4] = { 0.12f, 0.15f, 0.18f, 0.9f };

    hud_ui_rect(PX(0.f), PY(0.f), PX(PAGE_W), PY(PAGE_H), bg);
    hud_ui_frame(PX(4.f), PY(4.f), PX(PAGE_W - 4.f), PY(PAGE_H - 4.f), 3.f * sc, edge);
    static const char *const titles[4] = { "MRIYA", "CAMERA", "ENVIRONMENT", "SETTINGS" };
    hud_ui_text(PX(PAGE_W * 0.5f), PY(46.f), titles[m->page & 3], 5.f * sc, 0, head, 1);

    for (int i = 0; i < n; ++i) {
        const Row *r = &rows[i];
        float y = row_y(i);
        if (i == m->hover)
            hud_ui_rect(PX(PAD), PY(y - 4.f), PX(PAGE_W - PAD), PY(y + ROW_H - 12.f), sel);
        const float *c = (r->kind == 4 || r->kind == 5) ? dim : txt;
        char label[96];
        snprintf(label, sizeof label, "%s%s%s", r->kind == 4 ? "< " : "",
                 r->kind == 5 ? (r->value < 0 ? "^ " : "v ") : "", r->label);
        hud_ui_text(PX(PAD + 16.f), PY(y + 8.f), label, 3.f * sc, -1, c, 1);
        if (r->kind == 2 && r->checked)
            hud_ui_text(PX(PAGE_W - PAD - 24.f), PY(y + 8.f), "ON", 3.f * sc, 1, head, 1);
        else if (r->right[0])
            hud_ui_text(PX(PAGE_W - PAD - 24.f), PY(y + 8.f), r->right, 3.f * sc, 1, dim, 1);
        if (r->kind == 3) {
            float x0 = PAGE_W * 0.52f, x1 = PAGE_W - PAD - 90.f;
            float yy = y + 16.f;
            hud_ui_rect(PX(x0), PY(yy - 6.f), PX(x1), PY(yy + 6.f), barbg);
            hud_ui_rect(PX(x0), PY(yy - 6.f),
                        PX(x0 + (x1 - x0) * clampf(r->frac, 0.f, 1.f)), PY(yy + 6.f), bar);
        }
    }
    if (total > vis) {
        char more[40];
        int last = m->scroll + vis < total ? m->scroll + vis : total;
        snprintf(more, sizeof more, "%d - %d of %d", m->scroll + 1, last, total);
        hud_ui_text(PX(PAGE_W * 0.5f), PY(PAGE_H - 18.f), more, 2.5f * sc, 0, dim, 1);
    }
#undef PX
#undef PY
}

void vrmenu_paint(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens) {
    Row rows[32];
    int total = 0, vis = 0;
    int n = page_rows(m, s, hud_on, camera, scene, lens, rows, 32, &total, &vis);
    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glViewport(0, 0, PAGE_W, PAGE_H);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    hud_ui_begin(PAGE_W, PAGE_H, 3.f);
    draw_page(m, rows, n, total, vis, 0.f, 0.f, 1.f);
    hud_ui_flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ---- the same menu on a monitor -----------------------------------------
 * In the screensaver F2 puts it up in the middle of the screen, and the mouse
 * works it instead of a controller. The page keeps its shape: as tall as
 * four fifths of the window, and never wider than the window allows. */
static void screen_rect(int win_w, int win_h, float *ox, float *oy, float *sc) {
    float s = fminf((float)win_h * 0.8f / (float)PAGE_H, (float)win_w * 0.8f / (float)PAGE_W);
    if (s < 0.1f) s = 0.1f;
    *sc = s;
    *ox = ((float)win_w - PAGE_W * s) * 0.5f;
    *oy = ((float)win_h - PAGE_H * s) * 0.5f;
}

int vrmenu_screen_uv(const VrMenu *m, int win_w, int win_h, float mx, float my,
                     float *u, float *v) {
    (void)m;
    float ox, oy, sc;
    screen_rect(win_w, win_h, &ox, &oy, &sc);
    float x = (mx - ox) / (PAGE_W * sc), y = (my - oy) / (PAGE_H * sc);
    if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) return 0;
    *u = x; *v = y;
    return 1;
}

void vrmenu_paint_screen(VrMenu *m, const Settings *s, int hud_on, int camera, int scene,
                         int lens, int win_w, int win_h) {
    Row rows[32];
    int total = 0, vis = 0;
    int n = page_rows(m, s, hud_on, camera, scene, lens, rows, 32, &total, &vis);
    float ox, oy, sc;
    screen_rect(win_w, win_h, &ox, &oy, &sc);
    hud_ui_begin(win_w, win_h, 1.f);
    draw_page(m, rows, n, total, vis, ox, oy, sc);
    hud_ui_flush();
}
