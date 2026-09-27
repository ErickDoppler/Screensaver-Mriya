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
/* How much of the headset's own view each shows. Narrower magnifies. */
static const float lens_zoom[LENS_COUNT] = { 0.62f, 1.0f, 0.35f, 1.35f, 1.6f };

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
    return ok;
}

void vrmenu_shutdown(VrMenu *m) {
    if (m->tex) glDeleteTextures(1, &m->tex);
    if (m->fbo) glDeleteFramebuffers(1, &m->fbo);
    m->tex = m->fbo = 0;
}

void vrmenu_open(VrMenu *m, int open) {
    m->open = open;
    if (!open) { m->page = 0; m->scroll = 0; m->drag = -1.f; m->hover = -1; }
}

/* ---- the layout ---------------------------------------------------------
 * Every page is a list of rows, and a row's job is either to go somewhere, to
 * pick something, or to be a slider. The same function lays them out and
 * answers what is under the pointer, so the two can never disagree. */
typedef struct Row {
    const char *label;
    int kind;        /* 0 plain, 1 submenu, 2 pick, 3 slider, 4 back */
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
            if (s->quality == 0) snprintf(out[n].right, sizeof out[n].right, "auto");
            else snprintf(out[n].right, sizeof out[n].right, "%d", s->quality);
            n++;
        }
        if (n < cap) {
            out[n] = (Row){ "Autopilot takes over after", 3, VRMENU_AUTOPILOT, 0, 0,
                            (s->autopilot_resume - 5) / 595.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%d s", s->autopilot_resume);
            n++;
        }
        if (n < cap) {
            out[n] = (Row){ "Lens", 2, VRMENU_LENS, (lens + 1) % LENS_COUNT, 0, 0.f, "" };
            snprintf(out[n].right, sizeof out[n].right, "%s", vrmenu_lens_name(lens));
            n++;
        }
    }
    return n;
}

/* How many rows fit, and where each one sits on the page. */
static int rows_visible(void) { return (int)((PAGE_H - TOP - PAD) / ROW_H); }
static float row_y(int i) { return TOP + (float)i * ROW_H; }

static int row_at(const VrMenu *m, float u, float v, int count) {
    float x = u * PAGE_W, y = v * PAGE_H;
    if (x < PAD || x > PAGE_W - PAD) return -1;
    int vis = rows_visible();
    for (int i = 0; i < vis; ++i) {
        int idx = m->scroll + i;
        if (idx >= count) break;
        float ry = row_y(i);
        if (y >= ry && y < ry + ROW_H - 6.f) return idx;
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
    Row rows[64];
    int n = rows_of(m, s, hud_on, camera, scene, lens, rows, 64);
    (void)value;
    m->hover = row_at(m, u, v, n);
    return VRMENU_NONE;
}

/* The rows are built from the real settings, so a slider can be read off
 * wherever the pointer is. */
int vrmenu_click(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens,
                 float u, float v, int *value) {
    Row rows[64];
    int n = rows_of(m, s, hud_on, camera, scene, lens, rows, 64);
    int idx = row_at(m, u, v, n);
    m->hover = idx;
    if (idx < 0) return VRMENU_NONE;
    Row *r = &rows[idx];
    if (r->kind == 4) { m->page = 0; m->scroll = 0; return VRMENU_NONE; }
    if (r->kind == 1) { m->page = r->value; m->scroll = 0; return VRMENU_NONE; }
    if (r->kind == 3) {
        m->drag = (float)idx;
        float x0 = PAGE_W * 0.52f, x1 = PAGE_W - PAD - 90.f;
        float f = clampf((u * PAGE_W - x0) / (x1 - x0), 0.f, 1.f);
        *value = r->action == VRMENU_QUALITY ? (int)(f * 100.f + 0.5f) : (int)(5.f + f * 595.f);
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
    /* which slider is being dragged is remembered by its row */
    int idx = (int)m->drag;
    if (m->page != 3) return VRMENU_NONE;
    if (idx == 1) { *value = (int)(f * 100.f + 0.5f); return VRMENU_QUALITY; }
    if (idx == 2) { *value = (int)(5.f + f * 595.f); return VRMENU_AUTOPILOT; }
    return VRMENU_NONE;
}

void vrmenu_release(VrMenu *m) { m->drag = -1.f; }

/* ---- drawing ------------------------------------------------------------- */
void vrmenu_paint(VrMenu *m, const Settings *s, int hud_on, int camera, int scene, int lens) {
    Row rows[64];
    int n = rows_of(m, s, hud_on, camera, scene, lens, rows, 64);
    int vis = rows_visible();
    if (m->hover >= 0) {
        if (m->hover < m->scroll) m->scroll = m->hover;
        if (m->hover >= m->scroll + vis) m->scroll = m->hover - vis + 1;
    }
    if (m->scroll > n - vis) m->scroll = n - vis;
    if (m->scroll < 0) m->scroll = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, m->fbo);
    glViewport(0, 0, PAGE_W, PAGE_H);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    hud_ui_begin(PAGE_W, PAGE_H, 3.f);

    const float bg[4]    = { 0.03f, 0.05f, 0.07f, 0.88f };
    const float edge[4]  = { 0.45f, 0.85f, 0.6f,  0.85f };
    const float head[4]  = { 1.0f,  0.82f, 0.25f, 1.f };
    const float txt[4]   = { 0.88f, 0.92f, 0.95f, 1.f };
    const float dim[4]   = { 0.55f, 0.62f, 0.68f, 1.f };
    const float sel[4]   = { 0.12f, 0.35f, 0.25f, 0.9f };
    const float bar[4]   = { 0.25f, 0.45f, 0.35f, 0.9f };
    const float barbg[4] = { 0.12f, 0.15f, 0.18f, 0.9f };

    hud_ui_rect(0.f, 0.f, PAGE_W, PAGE_H, bg);
    hud_ui_frame(4.f, 4.f, PAGE_W - 4.f, PAGE_H - 4.f, 3.f, edge);
    static const char *const titles[4] = { "MRIYA", "CAMERA", "ENVIRONMENT", "SETTINGS" };
    hud_ui_text(PAGE_W * 0.5f, 46.f, titles[m->page & 3], 5.f, 0, head, 1);

    for (int i = 0; i < vis; ++i) {
        int idx = m->scroll + i;
        if (idx >= n) break;
        Row *r = &rows[idx];
        float y = row_y(i);
        if (idx == m->hover) hud_ui_rect(PAD, y - 4.f, PAGE_W - PAD, y + ROW_H - 12.f, sel);
        const float *c = r->kind == 4 ? dim : txt;
        char label[96];
        snprintf(label, sizeof label, "%s%s", r->kind == 4 ? "< " : "", r->label);
        hud_ui_text(PAD + 16.f, y + 8.f, label, 3.f, -1, c, 1);
        if (r->kind == 2 && r->checked) hud_ui_text(PAGE_W - PAD - 24.f, y + 8.f, "ON", 3.f, 1, head, 1);
        else if (r->right[0]) hud_ui_text(PAGE_W - PAD - 24.f, y + 8.f, r->right, 3.f, 1, dim, 1);
        if (r->kind == 3) {
            float x0 = PAGE_W * 0.52f, x1 = PAGE_W - PAD - 90.f;
            float yy = y + 16.f;
            hud_ui_rect(x0, yy - 6.f, x1, yy + 6.f, barbg);
            hud_ui_rect(x0, yy - 6.f, x0 + (x1 - x0) * clampf(r->frac, 0.f, 1.f), yy + 6.f, bar);
        }
    }
    if (n > vis) {
        char more[32];
        snprintf(more, sizeof more, "%d - %d of %d", m->scroll + 1,
                 m->scroll + vis < n ? m->scroll + vis : n, n);
        hud_ui_text(PAGE_W * 0.5f, PAGE_H - 18.f, more, 2.5f, 0, dim, 1);
    }
    hud_ui_flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
