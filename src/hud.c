#include "hud.h"
#include "gl_loader.h"
#include "render.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern const char shader_common_glsl[], shader_hud_vert[], shader_hud_frag[];

/* --- the font: 5 x 7, drawn by hand, ASCII 32..95 plus a degree sign ------ */
static const char *const glyphs[65][7] = {
    { ".....", ".....", ".....", ".....", ".....", ".....", "....." }, /* space */
    { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." }, /* ! */
    { ".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....." }, /* " */
    { ".#.#.", "#####", ".#.#.", ".#.#.", "#####", ".#.#.", "....." }, /* # */
    { "..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.." }, /* $ */
    { "##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##" }, /* % */
    { ".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#" }, /* & */
    { "..#..", "..#..", ".....", ".....", ".....", ".....", "....." }, /* ' */
    { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#." }, /* ( */
    { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..." }, /* ) */
    { ".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....." }, /* * */
    { ".....", "..#..", "..#..", "#####", "..#..", "..#..", "....." }, /* + */
    { ".....", ".....", ".....", ".....", ".##..", "..#..", ".#..." }, /* , */
    { ".....", ".....", ".....", "#####", ".....", ".....", "....." }, /* - */
    { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." }, /* . */
    { "....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...." }, /* / */
    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." }, /* 0 */
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." }, /* 1 */
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" }, /* 2 */
    { "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###." }, /* 3 */
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." }, /* 4 */
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." }, /* 5 */
    { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." }, /* 6 */
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." }, /* 7 */
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." }, /* 8 */
    { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." }, /* 9 */
    { ".....", ".##..", ".##..", ".....", ".##..", ".##..", "....." }, /* : */
    { ".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..." }, /* ; */
    { "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#." }, /* < */
    { ".....", ".....", "#####", ".....", "#####", ".....", "....." }, /* = */
    { ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..." }, /* > */
    { ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.." }, /* ? */
    { ".###.", "#...#", "....#", ".##.#", "#.#.#", "#.#.#", ".###." }, /* @ */
    { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, /* A */
    { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." }, /* B */
    { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." }, /* C */
    { "###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.." }, /* D */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#####" }, /* E */
    { "#####", "#....", "#....", "####.", "#....", "#....", "#...." }, /* F */
    { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" }, /* G */
    { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, /* H */
    { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." }, /* I */
    { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." }, /* J */
    { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" }, /* K */
    { "#....", "#....", "#....", "#....", "#....", "#....", "#####" }, /* L */
    { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" }, /* M */
    { "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#" }, /* N */
    { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* O */
    { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." }, /* P */
    { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" }, /* Q */
    { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" }, /* R */
    { ".####", "#....", "#....", ".###.", "....#", "....#", "####." }, /* S */
    { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }, /* T */
    { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." }, /* U */
    { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." }, /* V */
    { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#." }, /* W */
    { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" }, /* X */
    { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." }, /* Y */
    { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" }, /* Z */
    { ".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###." }, /* [ */
    { "#....", ".#...", ".#...", "..#..", "...#.", "...#.", "....#" }, /* \ */
    { ".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###." }, /* ] */
    { "..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....." }, /* ^ */
    { ".....", ".....", ".....", ".....", ".....", ".....", "#####" }, /* _ */
    { ".##..", "#..#.", "#..#.", ".##..", ".....", ".....", "....." }, /* degree, as 0x7F */
};

#define CELL_W 6
#define CELL_H 8
#define ATLAS_COLS 16
#define ATLAS_ROWS 5

typedef struct { float x, y, u, v, r, g, b, a; } HVert;

static unsigned g_prog, g_vao, g_vbo, g_font;
static HVert *g_v;
static int g_n, g_cap;
static float g_scale;

int hud_init(void) {
    const char *vs[] = { shader_common_glsl, shader_hud_vert };
    const char *fs[] = { shader_common_glsl, shader_hud_frag };
    g_prog = render_program(vs, 2, fs, 2, "hud");
    if (!g_prog) return 0;
    unsigned char px[ATLAS_ROWS * CELL_H][ATLAS_COLS * CELL_W];
    memset(px, 0, sizeof px);
    for (int gi = 0; gi < 65; ++gi) {
        int cx = (gi % ATLAS_COLS) * CELL_W, cy = (gi / ATLAS_COLS) * CELL_H;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (glyphs[gi][y][x] == '#') px[cy + y][cx + x] = 255;
    }
    glGenTextures(1, &g_font);
    glBindTexture(GL_TEXTURE_2D, g_font);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_COLS * CELL_W, ATLAS_ROWS * CELL_H, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HVert), (void *)16);
    glBindVertexArray(0);
    return 1;
}

void hud_shutdown(void) {
    if (g_prog) glDeleteProgram(g_prog);
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    if (g_font) glDeleteTextures(1, &g_font);
    free(g_v);
    g_v = NULL;
    g_cap = g_n = 0;
}

/* --- primitives ------------------------------------------------------------ */
static void push(float x, float y, float u, float v, const float c[4]) {
    if (g_n >= g_cap) {
        g_cap = g_cap ? g_cap * 2 : 4096;
        g_v = realloc(g_v, sizeof *g_v * (size_t)g_cap);
    }
    HVert *h = &g_v[g_n++];
    h->x = x; h->y = y; h->u = u; h->v = v;
    h->r = c[0]; h->g = c[1]; h->b = c[2]; h->a = c[3];
}
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float c[4]) {
    push(x0, y0, u0, v0, c); push(x1, y0, u1, v0, c); push(x1, y1, u1, v1, c);
    push(x0, y0, u0, v0, c); push(x1, y1, u1, v1, c); push(x0, y1, u0, v1, c);
}
static void line(float x0, float y0, float x1, float y1, float w, const float c[4]) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    push(x0 + nx, y0 + ny, -1, 0, c); push(x1 + nx, y1 + ny, -1, 0, c); push(x1 - nx, y1 - ny, -1, 0, c);
    push(x0 + nx, y0 + ny, -1, 0, c); push(x1 - nx, y1 - ny, -1, 0, c); push(x0 - nx, y0 - ny, -1, 0, c);
}
static void rect_outline(float x0, float y0, float x1, float y1, float w, const float c[4]) {
    line(x0, y0, x1, y0, w, c); line(x1, y0, x1, y1, w, c);
    line(x1, y1, x0, y1, w, c); line(x0, y1, x0, y0, w, c);
}

static int glyph_index(unsigned char ch) {
    if (ch == 0x7F) return 64;
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
    if (ch < 32 || ch > 95) return 0;
    return ch - 32;
}

/* Text at pixel scale s; align -1 left, 0 centre, 1 right. */
static float text_width(const char *t, float s) { return (float)strlen(t) * CELL_W * s - s; }
static void text(float x, float y, const char *t, float s, int align, const float c[4], int shadow) {
    float w = text_width(t, s);
    if (align == 0) x -= w * 0.5f;
    else if (align > 0) x -= w;
    x = floorf(x); y = floorf(y);
    float aw = ATLAS_COLS * CELL_W, ah = ATLAS_ROWS * CELL_H;
    float sc[4] = { 0.f, 0.f, 0.f, c[3] * 0.6f };
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        float ox = pass == 0 ? s : 0.f, oy = pass == 0 ? s : 0.f;
        const float *col = pass == 0 ? sc : c;
        float cx = x;
        for (const char *p = t; *p; ++p) {
            int gi = glyph_index((unsigned char)*p);
            float u0 = (float)((gi % ATLAS_COLS) * CELL_W) / aw, v0 = (float)((gi / ATLAS_COLS) * CELL_H) / ah;
            float u1 = u0 + 5.f / aw, v1 = v0 + 7.f / ah;
            quad(cx + ox, y + oy, cx + ox + 5.f * s, y + oy + 7.f * s, u0, v0, u1, v1, col);
            cx += CELL_W * s;
        }
    }
}

/* --- the flight HUD -------------------------------------------------------- */
static int project(const HudInfo *h, vec3 d, float *x, float *y) {
    basis3 b = h->cam_basis;
    float cx = v3_dot(d, b.x), cy = v3_dot(d, b.y), cz = -v3_dot(d, b.z);
    if (cz < 0.05f) return 0;
    float th = tanf(h->fov * 0.5f), aspect = (float)h->width / (float)h->height;
    *x = (0.5f + 0.5f * (cx / cz) / (th * aspect)) * (float)h->width;
    *y = (0.5f - 0.5f * (cy / cz) / th) * (float)h->height;
    return 1;
}

static void flight_hud(const HudInfo *h, float s, float alpha) {
    const Flight *f = h->fl;
    float W = (float)h->width, H = (float)h->height;
    float g[4] = { 0.45f, 1.f, 0.55f, 0.92f * alpha };
    float gd[4] = { 0.45f, 1.f, 0.55f, 0.5f * alpha };
    float amber[4] = { 1.f, 0.72f, 0.2f, 0.95f * alpha };
    float lw = fmaxf(1.f, s * 0.5f);
    int imp = h->units == UNITS_IMPERIAL;
    char buf[96];

    /* pitch ladder and horizon, drawn in the world so they sit where the
     * horizon really is */
    double hd = flight_heading(f);
    vec3 fh = v3((float)sin(hd), 0.f, (float)-cos(hd));
    vec3 rh = v3((float)cos(hd), 0.f, (float)sin(hd));
    for (int p = -30; p <= 30; p += 5) {
        float pr = DEG2RAD((float)p);
        vec3 c = v3_add(v3_scale(fh, cosf(pr)), v3(0.f, sinf(pr), 0.f));
        float half = p == 0 ? 0.6f : 0.12f;
        vec3 a = v3_norm(v3_add(c, v3_scale(rh, -half))), b = v3_norm(v3_add(c, v3_scale(rh, half)));
        vec3 a2 = v3_norm(v3_add(c, v3_scale(rh, -half * 0.35f))), b2 = v3_norm(v3_add(c, v3_scale(rh, half * 0.35f)));
        float x0, y0, x1, y1, x2, y2, x3, y3;
        if (!project(h, a, &x0, &y0) || !project(h, b, &x1, &y1)) continue;
        if (p == 0) {
            line(x0, y0, x1, y1, lw, gd);
            continue;
        }
        project(h, a2, &x2, &y2);
        project(h, b2, &x3, &y3);
        float* col = gd;
        line(x0, y0, x2, y2, lw, col);
        line(x3, y3, x1, y1, lw, col);
        snprintf(buf, sizeof buf, "%d", p);
        text(x1 + 4.f * s, y1 - 3.5f * s, buf, s * 0.75f, -1, gd, 0);
    }
    /* the flight path marker: where the aircraft is actually going */
    {
        vec3 v = v3((float)(cos(f->gamma) * sin(f->track)), (float)sin(f->gamma), (float)(-cos(f->gamma) * cos(f->track)));
        float x, y;
        if (project(h, v, &x, &y)) {
            float r = 5.f * s;
            for (int k = 0; k < 12; ++k) {
                float a0 = k * MR_PI / 6.f, a1 = (k + 1) * MR_PI / 6.f;
                line(x + cosf(a0) * r, y + sinf(a0) * r, x + cosf(a1) * r, y + sinf(a1) * r, lw, g);
            }
            line(x - r * 2.6f, y, x - r, y, lw, g);
            line(x + r, y, x + r * 2.6f, y, lw, g);
            line(x, y - r, x, y - r * 1.8f, lw, g);
        }
    }

    /* speed, left */
    float tape_h = H * 0.36f, tape_w = 46.f * s;
    float lx = W * 0.19f, cy = H * 0.5f;
    double kts = f->speed * 1.943844;
    double shown_speed = imp ? kts : f->speed * 3.6;
    rect_outline(lx - tape_w, cy - tape_h * 0.5f, lx, cy + tape_h * 0.5f, lw, gd);
    {
        double step = imp ? 10.0 : 20.0;
        double px_per = tape_h / (step * 8.0);
        double base = floor(shown_speed / step) * step;
        for (int k = -5; k <= 5; ++k) {
            double v = base + k * step;
            float y = cy - (float)((v - shown_speed) * px_per);
            if (y < cy - tape_h * 0.5f + 4 || y > cy + tape_h * 0.5f - 4) continue;
            line(lx - 8.f * s, y, lx, y, lw, gd);
            if (((long)(v / step)) % 2 == 0) {
                snprintf(buf, sizeof buf, "%d", (int)v);
                text(lx - 10.f * s, y - 3.5f * s, buf, s * 0.8f, 1, gd, 0);
            }
        }
    }
    snprintf(buf, sizeof buf, "%d", (int)(shown_speed + 0.5));
    quad(lx - tape_w - 6.f * s, cy - 7.f * s, lx + 2.f * s, cy + 7.f * s, -1, 0, -1, 0, (float[4]){ 0.f, 0.f, 0.f, 0.55f * alpha });
    rect_outline(lx - tape_w - 6.f * s, cy - 7.f * s, lx + 2.f * s, cy + 7.f * s, lw, g);
    text(lx - 2.f * s, cy - 5.f * s, buf, s * 1.4f, 1, g, 0);
    text(lx - tape_w * 0.5f, cy - tape_h * 0.5f - 12.f * s, imp ? "KT TAS" : "KM/H TAS", s * 0.8f, 0, g, 1);
    snprintf(buf, sizeof buf, "M %.2f", f->mach);
    text(lx - tape_w * 0.5f, cy + tape_h * 0.5f + 5.f * s, buf, s * 0.9f, 0, g, 1);
    snprintf(buf, sizeof buf, "GS %d", (int)(imp ? f->ground_speed * 1.943844 : f->ground_speed * 3.6));
    text(lx - tape_w * 0.5f, cy + tape_h * 0.5f + 15.f * s, buf, s * 0.9f, 0, g, 1);

    /* altitude, right */
    float rx = W * 0.81f;
    double alt = imp ? f->pos.y * 3.28084 : f->pos.y;
    rect_outline(rx, cy - tape_h * 0.5f, rx + tape_w * 1.2f, cy + tape_h * 0.5f, lw, gd);
    {
        double step = imp ? 200.0 : 50.0;
        double px_per = tape_h / (step * 10.0);
        double base = floor(alt / step) * step;
        for (int k = -6; k <= 6; ++k) {
            double v = base + k * step;
            float y = cy - (float)((v - alt) * px_per);
            if (y < cy - tape_h * 0.5f + 4 || y > cy + tape_h * 0.5f - 4) continue;
            line(rx, y, rx + 8.f * s, y, lw, gd);
            if (((long)(v / step)) % 2 == 0) {
                snprintf(buf, sizeof buf, "%d", (int)v);
                text(rx + 10.f * s, y - 3.5f * s, buf, s * 0.8f, -1, gd, 0);
            }
        }
        /* the target altitude, as a bug on the tape */
        double tgt = imp ? f->alt_target * 3.28084 : f->alt_target;
        float ty = cy - (float)((tgt - alt) * px_per);
        ty = clampf(ty, cy - tape_h * 0.5f, cy + tape_h * 0.5f);
        line(rx - 6.f * s, ty - 4.f * s, rx, ty, lw, amber);
        line(rx - 6.f * s, ty + 4.f * s, rx, ty, lw, amber);
    }
    snprintf(buf, sizeof buf, "%d", (int)(alt + 0.5));
    quad(rx - 2.f * s, cy - 7.f * s, rx + tape_w * 1.2f + 8.f * s, cy + 7.f * s, -1, 0, -1, 0, (float[4]){ 0.f, 0.f, 0.f, 0.55f * alpha });
    rect_outline(rx - 2.f * s, cy - 7.f * s, rx + tape_w * 1.2f + 8.f * s, cy + 7.f * s, lw, g);
    text(rx + 3.f * s, cy - 5.f * s, buf, s * 1.4f, -1, g, 0);
    text(rx + tape_w * 0.6f, cy - tape_h * 0.5f - 12.f * s, imp ? "ALT FT" : "ALT M", s * 0.8f, 0, g, 1);
    double vs = imp ? f->vs * 196.85 : f->vs;
    if (imp) snprintf(buf, sizeof buf, "VS %+d FPM", (int)vs);
    else snprintf(buf, sizeof buf, "VS %+.1f M/S", vs);
    text(rx + tape_w * 0.6f, cy + tape_h * 0.5f + 5.f * s, buf, s * 0.9f, 0, g, 1);
    snprintf(buf, sizeof buf, "SEL %d", (int)(imp ? f->alt_target * 3.28084 : f->alt_target));
    text(rx + tape_w * 0.6f, cy + tape_h * 0.5f + 15.f * s, buf, s * 0.9f, 0, amber, 1);

    /* heading, top */
    {
        double hdg = wrapd(hd * 180.0 / MR_PI_D, 0.0, 360.0);
        float tx = W * 0.5f, ty = H * 0.1f, tw = W * 0.32f;
        double px_per = tw / 60.0;
        for (int k = -35; k <= 35; ++k) {
            double v = floor(hdg / 5.0) * 5.0 + k * 5.0;
            float x = tx + (float)((v - hdg) * px_per);
            if (x < tx - tw * 0.5f || x > tx + tw * 0.5f) continue;
            int iv = (int)wrapd(v, 0.0, 360.0);
            float tick = iv % 10 == 0 ? 7.f : 4.f;
            line(x, ty, x, ty + tick * s, lw, gd);
            if (iv % 10 == 0) {
                const char *lbl = NULL;
                if (iv == 0) lbl = "N"; else if (iv == 90) lbl = "E"; else if (iv == 180) lbl = "S"; else if (iv == 270) lbl = "W";
                if (lbl) snprintf(buf, sizeof buf, "%s", lbl);
                else snprintf(buf, sizeof buf, "%02d", iv / 10);
                text(x, ty - 10.f * s, buf, s * 0.8f, 0, gd, 0);
            }
        }
        line(tx, ty + 9.f * s, tx - 4.f * s, ty + 14.f * s, lw, g);
        line(tx, ty + 9.f * s, tx + 4.f * s, ty + 14.f * s, lw, g);
        snprintf(buf, sizeof buf, "%03d\x7F", (int)(hdg + 0.5) % 360);
        text(tx, ty + 17.f * s, buf, s * 1.2f, 0, g, 1);
    }

    /* modes, bottom */
    {
        float by = H * 0.86f;
        if (f->manual) {
            text(W * 0.5f, by, "MANUAL", s * 1.2f, 0, amber, 1);
            snprintf(buf, sizeof buf, "AUTOPILOT IN %d S", (int)fmax(0.0, (double)h->autopilot_resume - f->idle));
            text(W * 0.5f, by + 12.f * s, buf, s * 0.8f, 0, gd, 1);
        } else {
            text(W * 0.5f, by, "AUTOPILOT", s * 1.2f, 0, g, 1);
            if (f->phase == AP_LEG)
                snprintf(buf, sizeof buf, "LEG %d  -  %d %s TO TURN", f->legs_done + 1,
                         (int)(fmax(0.0, f->leg_len - f->leg_flown) / (imp ? 1852.0 : 1000.0)), imp ? "NM" : "KM");
            else
                snprintf(buf, sizeof buf, "TURNING RIGHT  %d\x7F", (int)(f->turned * 180.0 / MR_PI_D));
            text(W * 0.5f, by + 12.f * s, buf, s * 0.8f, 0, gd, 1);
        }
        if (f->floor_active) text(W * 0.5f, by - 14.f * s, "FLOOR - LEVELLING", s * 1.1f, 0, amber, 1);
        else if (f->stall_guard) text(W * 0.5f, by - 14.f * s, "ALPHA LIMIT", s * 1.1f, 0, amber, 1);
        else if (f->pos.y > f->ceiling - 150.0 && f->vs > 0.2)
            text(W * 0.5f, by - 14.f * s, "CEILING", s * 1.1f, 0, amber, 1);
        snprintf(buf, sizeof buf, "THR %d%%   BANK %d\x7F   G %.2f", (int)(f->throttle * 100.0 + 0.5),
                 (int)(f->bank * 180.0 / MR_PI_D), f->n + f->gust_n);
        text(W * 0.5f, by + 24.f * s, buf, s * 0.8f, 0, gd, 1);
    }
}

/* F1: every key and combination, in a panel in the middle of the screen. */
static const char *const help_rows[][2] = {
    { "FLYING", NULL },
    { "W / S", "nose down / nose up" },
    { "A / D", "roll left / right" },
    { "LEFT / RIGHT", "rudder" },
    { "PGUP / PGDN", "more / less power" },
    { "CTRL+A", "autopilot on / off" },
    { "", "the autopilot takes over after 20 s untouched" },
    { "CAMERAS", NULL },
    { "1 - 9, 0, -, =", "cameras: 1 nose ... - chase, = wingman" },
    { "SAME KEY AGAIN", "turn round / other side of the aircraft" },
    { "- AGAIN", "the globe round the aircraft (also G)" },
    { "CTRL+ALT+C", "next camera" },
    { "MOUSE", "look round (globe: orbit)" },
    { "MOUSE WHEEL", "zoom (globe: distance)" },
    { "HOME / R", "look straight again" },
    { "SCENERY AND SCREEN", NULL },
    { "CTRL+ALT+S", "next weather (real time: fetch it now)" },
    { "H", "flight HUD on / off" },
    { "PRINT SCREEN", "screenshot to Pictures\\Mriya" },
    { "F1", "this help" },
    { "ESC", "exit" },
};
static const char *const help_joy[][2] = {
    { "JOYSTICK", NULL },
    { "STICK", "roll and pitch; twist: rudder" },
    { "THROTTLE", "power" },
    { "BUTTON 2", "autopilot on / off" },
};

static void help_panel(const HudInfo *h, float s, float alpha) {
    int nrows = (int)(sizeof help_rows / sizeof *help_rows);
    int njoy = h->joystick ? (int)(sizeof help_joy / sizeof *help_joy) : 0;
    float ts = s * 1.0f;                           /* text scale */
    float row = 11.f * ts;
    /* sized to what it holds: the widest key, the widest line, every row */
    float kw = 0.f, vw = 0.f;
    int heads = 0;
    for (int pass = 0; pass < 2; ++pass) {
        int n = pass == 0 ? nrows : njoy;
        for (int i = 0; i < n; ++i) {
            const char *k = pass == 0 ? help_rows[i][0] : help_joy[i][0];
            const char *v = pass == 0 ? help_rows[i][1] : help_joy[i][1];
            if (!v) { heads++; continue; }
            kw = fmaxf(kw, text_width(k, ts));
            vw = fmaxf(vw, text_width(v, ts));
        }
    }
    float pad = 14.f * ts, gap = 18.f * ts;
    float colk = kw + gap;
    float wpan = pad * 2.f + colk + vw;
    float hpan = row * 0.9f + row * 1.6f + (float)(nrows + njoy) * row + (float)heads * row * 0.35f + row * 0.6f;
    float W = (float)h->width, H = (float)h->height;
    float x0 = floorf(W * 0.5f - wpan * 0.5f), y0 = floorf(H * 0.5f - hpan * 0.5f);
    float bg[4] = { 0.02f, 0.03f, 0.05f, 0.72f * alpha };
    float edge[4] = { 0.55f, 0.85f, 0.6f, 0.5f * alpha };
    float head[4] = { 1.0f, 0.82f, 0.25f, alpha };
    float key[4] = { 0.6f, 1.0f, 0.65f, alpha };
    float txt[4] = { 0.92f, 0.94f, 0.96f, alpha };
    quad(x0, y0, x0 + wpan, y0 + hpan, -1, 0, -1, 0, bg);
    rect_outline(x0, y0, x0 + wpan, y0 + hpan, fmaxf(1.f, s), edge);
    float x = x0 + pad, y = y0 + row * 0.9f;
    text(W * 0.5f, y, "KEYS", ts * 1.5f, 0, head, 1);
    y += row * 1.6f;
    for (int pass = 0; pass < 2; ++pass) {
        int n = pass == 0 ? nrows : njoy;
        for (int i = 0; i < n; ++i) {
            const char *k = pass == 0 ? help_rows[i][0] : help_joy[i][0];
            const char *v = pass == 0 ? help_rows[i][1] : help_joy[i][1];
            if (!v) {
                y += row * 0.35f;
                text(x, y, k, ts, -1, head, 1);
            } else {
                text(x, y, k, ts, -1, key, 1);
                text(x + colk, y, v, ts, -1, txt, 1);
            }
            y += row;
        }
    }
}

void hud_draw(const HudInfo *h) {
    if (h->fade >= 0.99f) return;
    g_n = 0;
    g_scale = fmaxf(1.f, floorf((float)h->height / 400.f + 0.5f));
    float s = g_scale;
    float vis = 1.f - h->fade;
    if (h->show_flight) flight_hud(h, s, vis * (1.f - h->passage * 0.8f));
    if (h->show_help) help_panel(h, s, fmaxf(vis, 0.6f));
    if (h->caption && h->caption_alpha > 0.01f) {
        float a = h->caption_alpha * vis;
        float wc[4] = { 1.f, 1.f, 1.f, 0.95f * a };
        float wd[4] = { 1.f, 1.f, 1.f, 0.7f * a };
        /* above the flight HUD's status lines when it is up */
        float y = (float)h->height * (h->show_flight ? 0.76f : 0.9f);
        text((float)h->width * 0.5f, y - 22.f * s, h->caption, s * 2.f, 0, wc, 1);
        if (h->subcaption) text((float)h->width * 0.5f, y, h->subcaption, s * 1.f, 0, wd, 1);
    }
    if (g_n == 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, h->width, h->height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_prog);
    glUniform2f(glGetUniformLocation(g_prog, "uRes"), (float)h->width, (float)h->height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_font);
    glUniform1i(glGetUniformLocation(g_prog, "uFont"), 0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(HVert) * (size_t)g_n), g_v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_n);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}
