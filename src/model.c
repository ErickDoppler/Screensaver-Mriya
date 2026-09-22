#include "model.h"
#include "gl_loader.h"
#include "platform.h"
#include "decals_atlas.h"
#include "bands_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Linked in by the generated blobs.S. */
extern const unsigned char mr_mesh_blob[], mr_mesh_blob_end[];
extern const unsigned char mr_decal_blob[], mr_decal_blob_end[];

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The livery atlas: run-length coded RGBA rows (tools/make_decals.py). */
static int load_decals(Model *m) {
    const unsigned char *p = mr_decal_blob, *end = mr_decal_blob_end;
    if (end - p < 12 || memcmp(p, "MRT1", 4)) { plat_log("decal atlas: bad header"); return 0; }
    int w = (int)rd32(p + 4), h = (int)rd32(p + 8);
    p += 12;
    unsigned char *px = malloc((size_t)w * h * 4);
    if (!px) return 0;
    for (int y = 0; y < h; ++y) {
        if (end - p < 2) { free(px); plat_log("decal atlas: truncated"); return 0; }
        int runs = rd16(p);
        p += 2;
        int x = 0;
        /* GL rows go bottom-up; the atlas rectangles are given top-down, so
         * the rows are stored top-down and the shader flips v. */
        unsigned char *row = px + (size_t)y * w * 4;
        for (int r = 0; r < runs; ++r) {
            if (end - p < 6) { free(px); plat_log("decal atlas: truncated run"); return 0; }
            int len = rd16(p);
            for (int k = 0; k < len && x < w; ++k, ++x) memcpy(row + x * 4, p + 2, 4);
            p += 6;
        }
    }
    glGenTextures(1, &m->decal_tex);
    glBindTexture(GL_TEXTURE_2D, m->decal_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* the paint colours are sRGB; the shader wants them linear */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(px);
    m->decal_w = w;
    m->decal_h = h;
    return 1;
}

/* The fuselage's cheatline: its edges along the fuselage, as a little
 * texture the shader filters between rows - row 0 the bands, row 1 the
 * grey belly's top. */
static int load_skin(Model *m) {
    static float px[2][FUS_BAND_N][4];
    for (int i = 0; i < FUS_BAND_N; ++i) {
        for (int k = 0; k < 4; ++k) px[0][i][k] = fus_band_table[i][k];
        px[1][i][0] = fus_band_table[i][4];
    }
    glGenTextures(1, &m->skin_tex);
    glBindTexture(GL_TEXTURE_2D, m->skin_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, FUS_BAND_N, 2, 0, GL_RGBA, GL_FLOAT, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

int model_load(Model *m) {
    memset(m, 0, sizeof *m);
    const unsigned char *p = mr_mesh_blob;
    size_t size = (size_t)(mr_mesh_blob_end - mr_mesh_blob);
    if (size < 36 || memcmp(p, "MRM1", 4)) { plat_log("mesh: bad header"); return 0; }
    uint32_t nv = rd32(p + 4), ni = rd32(p + 8);
    memcpy(m->center, p + 12, 12);
    memcpy(m->half, p + 24, 12);
    size_t vbytes = (size_t)nv * 12, ibytes = (size_t)ni * 4;
    if (36 + vbytes + ibytes > size) { plat_log("mesh: truncated"); return 0; }

    glGenVertexArrays(1, &m->vao);
    glGenBuffers(1, &m->vbo);
    glGenBuffers(1, &m->ibo);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbytes, p + 36, GL_STATIC_DRAW);
    /* attribute 0: x, y, z as 16-bit fractions of the box, and the part */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, 12, (void *)0);
    /* attribute 1: the normal, signed bytes */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 12, (void *)8);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ibytes, p + 36 + vbytes, GL_STATIC_DRAW);
    glBindVertexArray(0);
    m->index_count = (int)ni;
    plat_log("aircraft: %u vertices, %u triangles", nv, ni / 3);
    return load_decals(m) && load_skin(m);
}

void model_draw(const Model *m) {
    glBindVertexArray(m->vao);
    glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
}

void model_free(Model *m) {
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->ibo) glDeleteBuffers(1, &m->ibo);
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    if (m->decal_tex) glDeleteTextures(1, &m->decal_tex);
    if (m->skin_tex) glDeleteTextures(1, &m->skin_tex);
    memset(m, 0, sizeof *m);
}

/* ---------------------------------------------------------------------------
 * The livery of UR-82060 in its final Antonov Airlines scheme, laid out from
 * the kit sheet and photographs. Each decal is placed by its centre on the
 * side of the aircraft (z along the fuselage, aft positive; y up) and its
 * width; the height follows from the decal's own proportions.
 *
 * A decal painted on a right-facing surface is read with the nose to the
 * viewer's right, so its u runs toward -z; on the left it runs toward +z.
 * That is how the sheet's handed pairs (FUS_R / FUS_L ...) are drawn, and it
 * keeps lettering readable on both sides.
 * ------------------------------------------------------------------------- */
static void put(Decal *out, int *n, int cap,
                float u0, float v0, float u1, float v1, float aspect,
                float zc, float yc, float w, float h, float side, int part, float xc, float reach,
                float angle_deg) {
    if (*n >= cap) return;
    Decal *d = &out[(*n)++];
    d->u0 = u0; d->v0 = v0; d->u1 = u1; d->v1 = v1;
    d->zc = zc; d->yc = yc; d->w = w; d->h = h > 0.f ? h : w / aspect;
    d->side = side; d->part = (float)part; d->xc = xc; d->reach = reach;
    d->angle = angle_deg * (MR_PI / 180.f);
    d->axis = 0.f;
}

/* A decal projected from below, onto what faces the ground. Placed by its
 * centre (x across, z along) and its width across x; read from below with
 * the nose at the top, so its u runs toward +x and its v (down the image)
 * toward the tail. */
static void put_below(Decal *out, int *n, int cap,
                      float u0, float v0, float u1, float v1, float aspect,
                      float xc, float zc, float w, int part, float angle_deg) {
    if (*n >= cap) return;
    Decal *d = &out[(*n)++];
    d->u0 = u0; d->v0 = v0; d->u1 = u1; d->v1 = v1;
    d->zc = zc; d->yc = 0.f; d->w = w; d->h = w / aspect;
    d->side = 0.f; d->part = (float)part; d->xc = xc; d->reach = 0.f;
    d->angle = angle_deg * (MR_PI / 180.f);
    d->axis = 1.f;
}

int model_livery(Decal *out, int cap) {
    int n = 0;
    /* The fuselage's paint is a texture (tools/make_skin.py); its titles
     * are painted out of it and drawn here instead, crisp, where that
     * texture has them: ANTONOV 225 under the windscreen and the cargo
     * title behind the cockpit on both sides, the name - with the flag and
     * emblem the texture keeps - on the left only. */
    put(out, &n, cap, DECAL_AN225, -35.54f, -0.30f, 3.55f, 0.f, +1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    put(out, &n, cap, DECAL_AN225, -35.53f, -0.23f, 3.55f, 0.f, -1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    put(out, &n, cap, DECAL_ICT, -29.72f, 1.29f, 5.5f, 0.f, +1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    put(out, &n, cap, DECAL_ICT, -29.79f, 1.30f, 5.5f, 0.f, -1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    put(out, &n, cap, DECAL_MRIYA, -29.64f, -0.12f, 2.15f, 0.f, -1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    /* the flag just behind the name, on the left, where the texture had it */
    put(out, &n, cap, DECAL_FLAG, -27.3f, -0.05f, 0.95f, 0.f, -1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    /* and from below, on the grey behind the nose's blue arc, the chin's
     * lettering - where the texture had it */
    put_below(out, &n, cap, DECAL_CHIN, 0.f, -35.95f, 3.0f, PART_FUSELAGE, 180.f);   /* letter tops toward the nose */
    /* both faces of both fins: the swoosh across the lower trailing edge and
     * the roundel above it */
    for (int f = -1; f <= 1; f += 2) {
        float xc = 15.0f * (float)f;
        put(out, &n, cap, DECAL_FIN_A, 38.4f, 7.4f, 10.5f, 0.f, +1.f, PART_FIN, xc, 2.f, 0.f);
        put(out, &n, cap, DECAL_FIN_B, 38.4f, 7.4f, 10.5f, 0.f, -1.f, PART_FIN, xc, 2.f, 0.f);
        put(out, &n, cap, DECAL_ROUNDEL, 38.6f, 9.4f, 3.7f, 0.f, +1.f, PART_FIN, xc, 2.f, 0.f);
        put(out, &n, cap, DECAL_ROUNDEL, 38.6f, 9.4f, 3.7f, 0.f, -1.f, PART_FIN, xc, 2.f, 0.f);
    }
    /* the six nacelles: the swoosh on each side of the cowl's rear half.
     * The cowls are 5.2 m long from the intake lip (ez here is 3.4 m back
     * from it); aft of the nozzle is only pylon. */
    static const float ex[3] = { 9.895f, 17.275f, 24.595f };
    static const float ez[3] = { -13.84f, -9.035f, -4.19f };
    static const float ey[3] = { -0.45f, -0.71f, -0.98f };
    for (int s = -1; s <= 1; s += 2)
        for (int e = 0; e < 3; ++e) {
            float xc = ex[e] * (float)s;
            /* (the swoosh itself is drawn in aircraft.frag, crisp) */
            (void)xc; (void)ez; (void)ey;
        }
    /* the registration under the left wing, at 79% of the span and mid
     * chord as on the AN225.fbx model, letter tops toward the leading edge */
    put_below(out, &n, cap, DECAL_REG, -34.7f, 5.35f, 4.6f, PART_WING, 0.f);
    /* and on both sides of the rear fuselage, just aft of the wing, in big
     * black letters - as the aircraft wears it (the FBX texture lacks it) */
    put(out, &n, cap, DECAL_REG, 14.0f, -0.3f, 5.6f, 0.f, +1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    put(out, &n, cap, DECAL_REG, 14.0f, -0.3f, 5.6f, 0.f, -1.f, PART_FUSELAGE, 0.f, 6.f, 0.f);
    return n;
}
