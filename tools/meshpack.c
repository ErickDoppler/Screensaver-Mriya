/* meshpack - turns res/models/Mriya.glb into the compact mesh the screensaver
 * embeds. Runs at build time on the build machine.
 *
 *   meshpack <in.glb> <out.mesh>
 *
 * What it does to the model, and why:
 *
 *  * Re-orients it. The export lies upside down with the nose along +Y, at
 *    0.1 m to the unit. The screensaver's body frame is metres, x = right
 *    wing, y = up, z = aft (so the nose points down -z, like an OpenGL
 *    camera), with the origin on the fuselage centreline at the wing root.
 *    That is a proper rotation (180 degrees about the length axis, then a
 *    quarter turn), so nothing is mirrored.
 *
 *  * Smooths it. The export is flat shaded - 810k vertices for 280k
 *    triangles, every one carrying its own face normal - which close up, with
 *    the camera bolted to the skin, reads as a faceted toy. Positions are
 *    welded and each corner gets the area-weighted average of the neighbouring
 *    faces that lie within CREASE_DEG of its own face, so the fuselage rounds
 *    off while the wing's trailing edge and the fin's corners stay sharp.
 *
 *  * Labels it. The paint scheme differs per part - fuselage, wing, engine,
 *    fin, tailplane - and the export has no materials. Every connected piece
 *    is classified by where it sits and how it is shaped, and the part number
 *    rides along in each vertex.
 *
 *  * Packs it. Positions as 16-bit fractions of the bounding box (1.4 mm
 *    steps), normals as signed bytes. 12 bytes a vertex instead of 24.
 *
 * Output layout (little endian):
 *   char  magic[4] = "MRM1"
 *   u32   vertex_count, index_count
 *   f32   center[3], half[3]            position = center + q / 32767 * half
 *   { i16 x, y, z, part; i8 nx, ny, nz, 0; } vertices[vertex_count]
 *   u32   indices[index_count]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

#define CREASE_DEG 38.0
#define MODEL_SCALE 0.1           /* export units to metres */
#define WELD_GRID   0.0005        /* metres */

/* Body-frame origin, in export units: the fuselage centreline (x, z) at the
 * wing root (y). */
#define ORIGIN_X (-180.0)
#define ORIGIN_Y (500.0)
#define ORIGIN_Z (-175.0)

enum { PART_FUSELAGE = 0, PART_WING = 1, PART_ENGINE = 2, PART_FIN = 3, PART_STAB = 4,
       PART_GLASS = 5 /* the windscreen panes: fuselage, made of glass */ };
static const char *const part_name[] = { "fuselage", "wing", "engine", "fin", "tailplane" };

static void die(const char *msg) { fprintf(stderr, "meshpack: %s\n", msg); exit(1); }

/* ------------------------------------------------------------------ JSON -- */
/* Just enough of a parser for a glTF header: objects, arrays, strings,
 * numbers, true/false/null. */
typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal {
    JType type;
    double num;
    char *str;
    struct JVal **items;      /* array items, or object values */
    char **keys;              /* object keys */
    int n;
} JVal;

static const char *jp;

static void skip_ws(void) { while (*jp && isspace((unsigned char)*jp)) jp++; }

static JVal *jnew(JType t) {
    JVal *v = calloc(1, sizeof *v);
    if (!v) die("out of memory");
    v->type = t;
    return v;
}

static char *parse_string_raw(void) {
    if (*jp != '"') die("bad JSON: expected string");
    jp++;
    size_t cap = 32, n = 0;
    char *s = malloc(cap);
    while (*jp && *jp != '"') {
        char c = *jp++;
        if (c == '\\') {
            char e = *jp++;
            if (e == 'u') { jp += 4; c = '?'; }
            else c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
        }
        if (n + 2 > cap) { cap *= 2; s = realloc(s, cap); }
        s[n++] = c;
    }
    if (*jp != '"') die("bad JSON: unterminated string");
    jp++;
    s[n] = 0;
    return s;
}

static JVal *parse_value(void);

static void push(JVal *v, char *key, JVal *item) {
    v->items = realloc(v->items, sizeof *v->items * (size_t)(v->n + 1));
    if (key) v->keys = realloc(v->keys, sizeof *v->keys * (size_t)(v->n + 1));
    v->items[v->n] = item;
    if (key) v->keys[v->n] = key;
    v->n++;
}

static JVal *parse_value(void) {
    skip_ws();
    if (*jp == '{') {
        JVal *v = jnew(J_OBJ);
        jp++;
        skip_ws();
        if (*jp == '}') { jp++; return v; }
        for (;;) {
            skip_ws();
            char *k = parse_string_raw();
            skip_ws();
            if (*jp++ != ':') die("bad JSON: expected ':'");
            push(v, k, parse_value());
            skip_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == '}') { jp++; return v; }
            die("bad JSON: expected ',' or '}'");
        }
    }
    if (*jp == '[') {
        JVal *v = jnew(J_ARR);
        jp++;
        skip_ws();
        if (*jp == ']') { jp++; return v; }
        for (;;) {
            push(v, NULL, parse_value());
            skip_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == ']') { jp++; return v; }
            die("bad JSON: expected ',' or ']'");
        }
    }
    if (*jp == '"') { JVal *v = jnew(J_STR); v->str = parse_string_raw(); return v; }
    if (!strncmp(jp, "true", 4))  { jp += 4; JVal *v = jnew(J_BOOL); v->num = 1; return v; }
    if (!strncmp(jp, "false", 5)) { jp += 5; return jnew(J_BOOL); }
    if (!strncmp(jp, "null", 4))  { jp += 4; return jnew(J_NULL); }
    char *end;
    double d = strtod(jp, &end);
    if (end == jp) die("bad JSON: unexpected character");
    jp = end;
    JVal *v = jnew(J_NUM);
    v->num = d;
    return v;
}

static JVal *jget(const JVal *o, const char *key) {
    if (!o || o->type != J_OBJ) return NULL;
    for (int i = 0; i < o->n; ++i) if (!strcmp(o->keys[i], key)) return o->items[i];
    return NULL;
}
static JVal *jat(const JVal *a, int i) {
    return (a && a->type == J_ARR && i >= 0 && i < a->n) ? a->items[i] : NULL;
}
static double jnum(const JVal *v, double def) { return (v && v->type == J_NUM) ? v->num : def; }

/* --------------------------------------------------------------- the GLB -- */
typedef struct { const uint8_t *data; size_t count; int comp_type; int comps; } Accessor;

static Accessor get_accessor(const JVal *root, const uint8_t *bin, size_t bin_len, int index) {
    const JVal *acc = jat(jget(root, "accessors"), index);
    if (!acc) die("missing accessor");
    const JVal *bv = jat(jget(root, "bufferViews"), (int)jnum(jget(acc, "bufferView"), -1));
    if (!bv) die("missing bufferView");
    size_t off = (size_t)jnum(jget(bv, "byteOffset"), 0) + (size_t)jnum(jget(acc, "byteOffset"), 0);
    if (jget(bv, "byteStride")) die("interleaved buffers are not supported");
    Accessor a;
    a.count = (size_t)jnum(jget(acc, "count"), 0);
    a.comp_type = (int)jnum(jget(acc, "componentType"), 0);
    const JVal *t = jget(acc, "type");
    a.comps = !t ? 1 : !strcmp(t->str, "VEC3") ? 3 : !strcmp(t->str, "VEC2") ? 2 :
              !strcmp(t->str, "VEC4") ? 4 : 1;
    size_t csize = (a.comp_type == 5126 || a.comp_type == 5125) ? 4 : a.comp_type == 5123 ? 2 : 1;
    if (off + a.count * a.comps * csize > bin_len) die("accessor runs past the buffer");
    a.data = bin + off;
    return a;
}

static uint32_t read_index(const Accessor *a, size_t i) {
    switch (a->comp_type) {
    case 5125: { uint32_t v; memcpy(&v, a->data + i * 4, 4); return v; }
    case 5123: { uint16_t v; memcpy(&v, a->data + i * 2, 2); return v; }
    default:   return a->data[i];
    }
}

/* ------------------------------------------------------------ geometry -- */
typedef struct { double x, y, z; } d3;
static d3 d3sub(d3 a, d3 b) { d3 r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
static d3 d3cross(d3 a, d3 b) {
    d3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}
static double d3dot(d3 a, d3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static d3 d3norm(d3 a) {
    double l = sqrt(d3dot(a, a));
    if (l < 1e-20) { d3 z = { 0, 1, 0 }; return z; }
    d3 r = { a.x / l, a.y / l, a.z / l };
    return r;
}

/* Export units -> body frame metres. See the header. */
static d3 to_body(double mx, double my, double mz) {
    d3 r;
    r.x = -(mx - ORIGIN_X) * MODEL_SCALE;
    r.y = -(mz - ORIGIN_Z) * MODEL_SCALE;
    r.z = -(my - ORIGIN_Y) * MODEL_SCALE;
    return r;
}

typedef struct { int64_t q[3]; uint32_t src; } WeldKey;
static int cmp_weld(const void *a, const void *b) {
    const WeldKey *x = a, *y = b;
    for (int k = 0; k < 3; ++k) {
        if (x->q[k] < y->q[k]) return -1;
        if (x->q[k] > y->q[k]) return 1;
    }
    return 0;
}

static uint32_t uf_find(uint32_t *p, uint32_t x) {
    uint32_t r = x;
    while (p[r] != r) r = p[r];
    while (p[x] != r) { uint32_t n = p[x]; p[x] = r; x = n; }
    return r;
}

/* Output vertex dedupe: open addressing on (position id, packed normal). */
typedef struct { uint32_t pos; uint32_t nrm; uint32_t out; int used; } Slot;

static uint32_t hash2(uint32_t a, uint32_t b) {
    uint64_t h = (uint64_t)a * 0x9E3779B97F4A7C15ull ^ ((uint64_t)b + 0x7F4A7C159E3779B9ull);
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
    return (uint32_t)h;
}

typedef struct { uint32_t a, b, f; } PEdge;
static int cmp_pedge(const void *x, const void *y) {
    const PEdge *p = x, *q = y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    if (p->b != q->b) return p->b < q->b ? -1 : 1;
    return 0;
}

static int8_t snorm8(double v) {
    double s = v * 127.0;
    s = s < -127 ? -127 : s > 127 ? 127 : s;
    return (int8_t)lround(s);
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: meshpack <in.glb> <out.mesh>\n"); return 2; }

    /* --- read the container ------------------------------------------ */
    FILE *f = fopen(argv[1], "rb");
    if (!f) die("cannot open the input");
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)flen);
    if (!file || fread(file, 1, (size_t)flen, f) != (size_t)flen) die("cannot read the input");
    fclose(f);
    if (flen < 28 || memcmp(file, "glTF", 4)) die("not a GLB file");
    uint32_t json_len, bin_len;
    memcpy(&json_len, file + 12, 4);
    if (memcmp(file + 16, "JSON", 4)) die("first chunk is not JSON");
    char *json = malloc(json_len + 1);
    memcpy(json, file + 20, json_len);
    json[json_len] = 0;
    size_t bin_hdr = 20 + json_len;
    memcpy(&bin_len, file + bin_hdr, 4);
    if (memcmp(file + bin_hdr + 4, "BIN", 3)) die("second chunk is not BIN");
    const uint8_t *bin = file + bin_hdr + 8;
    jp = json;
    JVal *root = parse_value();

    /* --- gather every triangle of every primitive --------------------- */
    const JVal *meshes = jget(root, "meshes");
    if (!meshes || meshes->n == 0) die("no meshes");
    size_t nsrc = 0, ntri = 0;
    d3 *src = NULL;               /* source positions, body frame */
    uint32_t *tri = NULL;         /* 3 source vertex ids per triangle */
    for (int m = 0; m < meshes->n; ++m) {
        const JVal *prims = jget(meshes->items[m], "primitives");
        for (int p = 0; prims && p < prims->n; ++p) {
            const JVal *pr = prims->items[p];
            const JVal *mode = jget(pr, "mode");
            if (mode && (int)mode->num != 4) continue;          /* triangles only */
            const JVal *attr = jget(pr, "attributes");
            Accessor pa = get_accessor(root, bin, bin_len, (int)jnum(jget(attr, "POSITION"), -1));
            if (pa.comp_type != 5126 || pa.comps != 3) die("positions must be float3");
            size_t base = nsrc;
            src = realloc(src, sizeof *src * (nsrc + pa.count));
            for (size_t i = 0; i < pa.count; ++i) {
                float v[3];
                memcpy(v, pa.data + i * 12, 12);
                src[nsrc++] = to_body(v[0], v[1], v[2]);
            }
            const JVal *ind = jget(pr, "indices");
            size_t n = ind ? get_accessor(root, bin, bin_len, (int)ind->num).count : pa.count;
            Accessor ia;
            if (ind) ia = get_accessor(root, bin, bin_len, (int)ind->num);
            tri = realloc(tri, sizeof *tri * (ntri * 3 + n));
            for (size_t i = 0; i + 2 < n; i += 3) {
                for (int k = 0; k < 3; ++k)
                    tri[ntri * 3 + k] = (uint32_t)(base + (ind ? read_index(&ia, i + k) : i + k));
                ntri++;
            }
        }
    }
    fprintf(stderr, "meshpack: %zu source vertices, %zu triangles\n", nsrc, ntri);

    /* --- weld positions ------------------------------------------------ */
    WeldKey *wk = malloc(sizeof *wk * nsrc);
    for (size_t i = 0; i < nsrc; ++i) {
        wk[i].q[0] = llround(src[i].x / WELD_GRID);
        wk[i].q[1] = llround(src[i].y / WELD_GRID);
        wk[i].q[2] = llround(src[i].z / WELD_GRID);
        wk[i].src = (uint32_t)i;
    }
    qsort(wk, nsrc, sizeof *wk, cmp_weld);
    uint32_t *pos_of = malloc(sizeof *pos_of * nsrc);   /* source vertex -> welded id */
    d3 *pos = malloc(sizeof *pos * nsrc);
    size_t npos = 0;
    for (size_t i = 0; i < nsrc; ++i) {
        if (i == 0 || cmp_weld(&wk[i], &wk[i - 1])) pos[npos++] = src[wk[i].src];
        pos_of[wk[i].src] = (uint32_t)(npos - 1);
    }
    free(wk);

    /* Drop triangles the weld collapsed. */
    uint32_t *T = malloc(sizeof *T * ntri * 3);
    size_t nt = 0;
    for (size_t t = 0; t < ntri; ++t) {
        uint32_t a = pos_of[tri[t * 3]], b = pos_of[tri[t * 3 + 1]], c = pos_of[tri[t * 3 + 2]];
        if (a == b || b == c || a == c) continue;
        T[nt * 3] = a; T[nt * 3 + 1] = b; T[nt * 3 + 2] = c;
        nt++;
    }
    fprintf(stderr, "meshpack: %zu welded positions, %zu triangles kept\n", npos, nt);

    /* --- face normals, and the faces around each position ------------- */
    d3 *fn = malloc(sizeof *fn * nt);      /* area-weighted */
    d3 *fu = malloc(sizeof *fu * nt);      /* unit */
    for (size_t t = 0; t < nt; ++t) {
        d3 a = pos[T[t * 3]], b = pos[T[t * 3 + 1]], c = pos[T[t * 3 + 2]];
        fn[t] = d3cross(d3sub(b, a), d3sub(c, a));
        fu[t] = d3norm(fn[t]);
    }
    uint32_t *start = calloc(npos + 1, sizeof *start);
    for (size_t i = 0; i < nt * 3; ++i) start[T[i] + 1]++;
    for (size_t i = 0; i < npos; ++i) start[i + 1] += start[i];
    uint32_t *fill = malloc(sizeof *fill * npos);
    memcpy(fill, start, sizeof *fill * npos);
    uint32_t *adj = malloc(sizeof *adj * nt * 3);
    for (size_t i = 0; i < nt * 3; ++i) adj[fill[T[i]]++] = (uint32_t)(i / 3);
    free(fill);

    /* --- connected pieces, and what each one is ----------------------- */
    uint32_t *uf = malloc(sizeof *uf * npos);
    for (size_t i = 0; i < npos; ++i) uf[i] = (uint32_t)i;
    for (size_t t = 0; t < nt; ++t) {
        uint32_t a = uf_find(uf, T[t * 3]);
        uint32_t b = uf_find(uf, T[t * 3 + 1]);
        uint32_t c = uf_find(uf, T[t * 3 + 2]);
        if (a != b) uf[b] = a;
        c = uf_find(uf, c);
        a = uf_find(uf, a);
        if (a != c) uf[c] = a;
    }
    d3 *bmin = malloc(sizeof *bmin * npos), *bmax = malloc(sizeof *bmax * npos);
    for (size_t i = 0; i < npos; ++i) {
        d3 big = { 1e30, 1e30, 1e30 }, small = { -1e30, -1e30, -1e30 };
        bmin[i] = big; bmax[i] = small;
    }
    for (size_t i = 0; i < npos; ++i) {
        uint32_t r = uf_find(uf, (uint32_t)i);
        d3 p = pos[i];
        if (p.x < bmin[r].x) bmin[r].x = p.x;
        if (p.y < bmin[r].y) bmin[r].y = p.y;
        if (p.z < bmin[r].z) bmin[r].z = p.z;
        if (p.x > bmax[r].x) bmax[r].x = p.x;
        if (p.y > bmax[r].y) bmax[r].y = p.y;
        if (p.z > bmax[r].z) bmax[r].z = p.z;
    }
    uint8_t *part_of_root = malloc(npos);
    int counts[5] = { 0 };
    for (size_t i = 0; i < npos; ++i) {
        if (uf_find(uf, (uint32_t)i) != i) continue;
        double dx = bmax[i].x - bmin[i].x, dy = bmax[i].y - bmin[i].y;
        double cx = 0.5 * (bmax[i].x + bmin[i].x);
        double zmin = bmin[i].z;        /* most forward point; aft is +z */
        uint8_t part;
        if (fabs(cx) < 4.8 && dx < 9.0)             part = PART_FUSELAGE;
        else if (dy > 6.0)                          part = PART_FIN;
        else if (zmin > 21.0 && fabs(cx) > 13.5)    part = PART_FIN;   /* rudder bits */
        else if (zmin > 21.0)                       part = PART_STAB;
        else if (dx > 10.0)                         part = PART_WING;
        else                                        part = PART_ENGINE;
        part_of_root[i] = part;
        counts[part]++;
    }
    for (int k = 0; k < 5; ++k) fprintf(stderr, "meshpack:   %-9s %d piece(s)\n", part_name[k], counts[k]);

    /* The nacelle cowls are welded to the wing through their pylons, so they
     * come out as part of the wing's piece. Anything of the wing's inside an
     * engine's cylinder is engine: that is where the nacelle livery goes and
     * where the paint turns from wing grey to cowl white. */
    uint8_t *vpart = malloc(npos);
    int moved = 0;
    for (size_t i = 0; i < npos; ++i) {
        uint8_t pt = part_of_root[uf_find(uf, (uint32_t)i)];
        if (pt == PART_WING) {
            static const double eng[3][4] = {
                /* axis x, axis y, front z, back z */
                {  9.895, -0.296, -17.35, -5.2 },
                { 17.275, -0.560, -12.55, -0.5 },
                { 24.595, -0.835,  -7.70,  4.6 },
            };
            d3 p = pos[i];
            for (int e = 0; e < 3; ++e) {
                double dx = fabs(p.x) - eng[e][0], dy = p.y - eng[e][1];
                if (dx * dx + dy * dy < 1.62 * 1.62 && p.z > eng[e][2] && p.z < eng[e][3]) {
                    pt = PART_ENGINE;
                    moved++;
                    break;
                }
            }
        }
        vpart[i] = pt;
    }
    fprintf(stderr, "meshpack: %d wing vertices moved to the engines\n", moved);

    /* --- smoothed corner normals, deduplicated into output vertices ---- */
    const double crease = cos(CREASE_DEG * 3.14159265358979 / 180.0);
    /* The lower nose is one smooth curve on the aircraft; the model folds it
     * into a flat chin panel and rounded sides, and at the default crease the
     * fold shows as a hard line through the paint. Smoothed much further there. */
    const double crease_nose = cos(70.0 * 3.14159265358979 / 180.0);
    uint8_t *nose = calloc(nt, 1);
    for (size_t t = 0; t < nt; ++t) {
        d3 a = pos[T[t * 3]], b = pos[T[t * 3 + 1]], c = pos[T[t * 3 + 2]];
        double cz = (a.z + b.z + c.z) / 3.0, cy = (a.y + b.y + c.y) / 3.0;
        nose[t] = cz < -30.0 && cy < 1.6 && part_of_root[uf_find(uf, T[t * 3])] == PART_FUSELAGE;
    }
    size_t cap = 1;
    while (cap < nt * 3 * 2) cap <<= 1;
    Slot *table = calloc(cap, sizeof *table);
    uint32_t *out_idx = malloc(sizeof *out_idx * nt * 3);
    uint32_t *vpos = malloc(sizeof *vpos * nt * 3);
    uint32_t *vnrm = malloc(sizeof *vnrm * nt * 3);
    size_t nv = 0;
    for (size_t t = 0; t < nt; ++t) {
        for (int k = 0; k < 3; ++k) {
            uint32_t p = T[t * 3 + k];
            d3 acc = { 0, 0, 0 };
            for (uint32_t j = start[p]; j < start[p + 1]; ++j) {
                uint32_t g = adj[j];
                if (d3dot(fu[t], fu[g]) < (nose[t] && nose[g] ? crease_nose : crease)) continue;
                acc.x += fn[g].x; acc.y += fn[g].y; acc.z += fn[g].z;
            }
            d3 n = d3norm(acc);
            uint32_t packed = (uint32_t)(uint8_t)snorm8(n.x) |
                              (uint32_t)(uint8_t)snorm8(n.y) << 8 |
                              (uint32_t)(uint8_t)snorm8(n.z) << 16;
            uint32_t h = hash2(p, packed) & (uint32_t)(cap - 1);
            while (table[h].used && !(table[h].pos == p && table[h].nrm == packed))
                h = (h + 1) & (uint32_t)(cap - 1);
            if (!table[h].used) {
                table[h].used = 1;
                table[h].pos = p;
                table[h].nrm = packed;
                table[h].out = (uint32_t)nv;
                vpos[nv] = p;
                vnrm[nv] = packed;
                nv++;
            }
            out_idx[t * 3 + k] = table[h].out;
        }
    }
    fprintf(stderr, "meshpack: %zu output vertices\n", nv);

    /* --- the exhaust plugs ----------------------------------------------
     * The model closes each engine's core with a flat disk where a real
     * turbofan has a cone - the plug, round which the hot core flow leaves.
     * The D-18T's is small and sits well inside the nozzle, out of sight from
     * all but straight behind: each disk is taken out, and in its place a
     * tube running a metre up into the engine, at its bottom a flat annulus
     * (the hot gap round the plug, where the turbine shows) and, on its
     * inner edge, a short cone pointing aft, with its own smooth normals. */
    {
        static const double eng_axis[3][2] = { { 9.895, -0.296 }, { 17.275, -0.560 }, { 24.595, -0.835 } };
        static const double cowl_end[3] = { -12.14, -7.335, -2.49 };
        const double PLUG_LEN = 0.5;       /* short: its tip stays in the nozzle */
        const double PLUG_R = 0.6;         /* of the core's radius */
        const double DEPTH = 1.0;          /* how far up the nozzle it all sits */
        uint8_t *gone = calloc(nt, 1);
        /* room for the cones: at most two new triangles' worth per removed one */
        size_t extra_cap = 4096;
        uint32_t *eidx = malloc(sizeof *eidx * extra_cap * 3);
        size_t ne = 0;
        int cones = 0;
        for (int side = -1; side <= 1; side += 2)
            for (int e = 0; e < 3; ++e) {
                double axx = side * eng_axis[e][0], axy = eng_axis[e][1];
                /* the disk's triangles: flat, facing along the axis, inside the core */
                size_t first = ne;
                typedef struct { uint32_t a, b; } Edge;
                Edge *edges = malloc(sizeof *edges * nt);   /* generous */
                size_t nedge = 0;
                double zc = 0.0; int nz = 0;
                for (size_t t = 0; t < nt; ++t) {
                    int ok = 1;
                    for (int k = 0; k < 3 && ok; ++k) {
                        d3 p = pos[vpos[out_idx[t * 3 + k]]];
                        double r = hypot(p.x - axx, p.y - axy);
                        if (r > 0.62 || p.z < cowl_end[e] + 0.3 || p.z > cowl_end[e] + 2.5) ok = 0;
                    }
                    if (!ok) continue;
                    d3 p0 = pos[vpos[out_idx[t * 3]]], p1 = pos[vpos[out_idx[t * 3 + 1]]], p2 = pos[vpos[out_idx[t * 3 + 2]]];
                    d3 fnrm = d3norm(d3cross(d3sub(p1, p0), d3sub(p2, p0)));
                    if (fabs(fnrm.z) < 0.95) continue;
                    gone[t] = 1;
                    zc += p0.z; nz++;
                    for (int k = 0; k < 3; ++k) {
                        Edge ed = { vpos[out_idx[t * 3 + k]], vpos[out_idx[t * 3 + (k + 1) % 3]] };
                        edges[nedge++] = ed;
                    }
                }
                if (nz == 0) { free(edges); continue; }
                zc /= nz;
                /* the rim: edges used once */
                d3 tip = { axx, axy, zc - DEPTH + PLUG_LEN };
                pos = realloc(pos, sizeof *pos * (npos + 1));
                vpart = realloc(vpart, npos + 1);
                pos[npos] = tip;
                vpart[npos] = PART_ENGINE;
                uint32_t tip_pos = (uint32_t)npos++;
                /* the cone's base: each rim point drawn in toward the axis */
                uint32_t rim_src[256], rim_in[256], rim_deep[256];
                int nrim = 0;
                for (size_t i = 0; i < nedge; ++i) {
                    int twin = 0;
                    for (size_t j = 0; j < nedge && !twin; ++j)
                        if (j != i && edges[j].a == edges[i].b && edges[j].b == edges[i].a) twin = 1;
                    if (twin) continue;
                    uint32_t ends[2] = { edges[i].a, edges[i].b }, in[2], deep[2];
                    for (int k = 0; k < 2; ++k) {
                        int f = -1;
                        for (int r = 0; r < nrim; ++r) if (rim_src[r] == ends[k]) { f = r; break; }
                        if (f < 0 && nrim < 256) {
                            d3 q = pos[ends[k]];
                            d3 qd = { q.x, q.y, q.z - DEPTH };
                            d3 qi = { axx + (q.x - axx) * PLUG_R, axy + (q.y - axy) * PLUG_R, q.z - DEPTH };
                            pos = realloc(pos, sizeof *pos * (npos + 2));
                            vpart = realloc(vpart, npos + 2);
                            pos[npos] = qd;
                            vpart[npos] = PART_ENGINE;
                            pos[npos + 1] = qi;
                            vpart[npos + 1] = PART_ENGINE;
                            rim_src[nrim] = ends[k];
                            rim_deep[nrim] = (uint32_t)npos;
                            rim_in[nrim] = (uint32_t)npos + 1;
                            npos += 2;
                            f = nrim++;
                        }
                        in[k] = f >= 0 ? rim_in[f] : ends[k];
                        deep[k] = f >= 0 ? rim_deep[f] : ends[k];
                    }
                    uint32_t pa = ends[0], pb = ends[1], ia = in[0], ib = in[1], da = deep[0], db = deep[1];
                    uint32_t flat = (uint32_t)(uint8_t)snorm8(0.0) | (uint32_t)(uint8_t)snorm8(0.0) << 8 |
                                    (uint32_t)(uint8_t)snorm8(1.0) << 16;
                    /* five triangles: the tube's wall (two), the annulus at its
                     * bottom (two) and the cone (one) */
                    uint32_t tris[5][3] = { { da, db, ib }, { da, ib, ia }, { ia, ib, tip_pos },
                                            { pa, pb, db }, { pa, db, da } };
                    for (int tt = 0; tt < 5; ++tt) {
                        uint32_t pv[3] = { tris[tt][0], tris[tt][1], tris[tt][2] };
                        uint32_t packed[3];
                        d3 mid = { 0.5 * (pos[ia].x + pos[ib].x), 0.5 * (pos[ia].y + pos[ib].y), 0 };
                        d3 want;
                        if (tt >= 3) {
                            /* the tube's wall faces in, toward the axis */
                            for (int k = 0; k < 3; ++k) {
                                d3 q = pos[pv[k]];
                                double rx = axx - q.x, ry = axy - q.y, rl = hypot(rx, ry);
                                if (rl < 1e-6) { rx = 0; ry = 1; rl = 1; }
                                packed[k] = (uint32_t)(uint8_t)snorm8(rx / rl) | (uint32_t)(uint8_t)snorm8(ry / rl) << 8 |
                                            (uint32_t)(uint8_t)snorm8(0.0) << 16;
                            }
                            d3 m2 = { 0.5 * (pos[pa].x + pos[pb].x), 0.5 * (pos[pa].y + pos[pb].y), 0 };
                            want = (d3){ axx - m2.x, axy - m2.y, 0 };
                        } else if (tt < 2) {
                            for (int k = 0; k < 3; ++k) packed[k] = flat;
                            want = (d3){ 0, 0, 1 };
                        } else {
                            for (int k = 0; k < 3; ++k) {
                                d3 q = k < 2 ? pos[pv[k]] : mid;
                                double rx = q.x - axx, ry = q.y - axy, rl = hypot(rx, ry);
                                if (rl < 1e-6) { rx = 0; ry = 1; rl = 1; }
                                double r0 = 0.57 * PLUG_R;
                                d3 n = d3norm((d3){ rx / rl * PLUG_LEN, ry / rl * PLUG_LEN, r0 });
                                packed[k] = (uint32_t)(uint8_t)snorm8(n.x) | (uint32_t)(uint8_t)snorm8(n.y) << 8 |
                                            (uint32_t)(uint8_t)snorm8(n.z) << 16;
                            }
                            want = (d3){ mid.x - axx, mid.y - axy, 0.3 };
                        }
                        /* wound so the geometric normal agrees with those normals */
                        d3 fnrm = d3cross(d3sub(pos[pv[1]], pos[pv[0]]), d3sub(pos[pv[2]], pos[pv[0]]));
                        if (d3dot(fnrm, want) < 0.0) {
                            uint32_t tp = pv[0]; pv[0] = pv[1]; pv[1] = tp;
                            uint32_t tn = packed[0]; packed[0] = packed[1]; packed[1] = tn;
                        }
                        if (ne + 1 > extra_cap) { extra_cap *= 2; eidx = realloc(eidx, sizeof *eidx * extra_cap * 3); }
                        for (int k = 0; k < 3; ++k) {
                            vpos = realloc(vpos, sizeof *vpos * (nv + 1));
                            vnrm = realloc(vnrm, sizeof *vnrm * (nv + 1));
                            vpos[nv] = pv[k];
                            vnrm[nv] = packed[k];
                            eidx[ne * 3 + k] = (uint32_t)nv++;
                        }
                        ne++;
                    }
                }
                free(edges);
                if (ne > first) cones++;
            }
        /* the triangles that stay, then the cones */
        size_t keep = 0;
        for (size_t t = 0; t < nt; ++t) {
            if (gone[t]) continue;
            for (int k = 0; k < 3; ++k) out_idx[keep * 3 + k] = out_idx[t * 3 + k];
            keep++;
        }
        out_idx = realloc(out_idx, sizeof *out_idx * (keep + ne) * 3);
        memcpy(out_idx + keep * 3, eidx, sizeof *eidx * ne * 3);
        fprintf(stderr, "meshpack: %d exhaust plugs: %zu flat triangles replaced by %zu of tube, annulus and cone\n",
                cones, nt - keep, ne);
        nt = keep + ne;
        free(eidx);
        free(gone);
    }

    /* --- the cockpit windows ----------------------------------------------
     * The model has the windscreen as recesses in the nose - each pane a
     * patch of skin sunk behind a frame, bounded by sharp creases where the
     * frame's walls meet it. Found here by growing smooth regions across the
     * cockpit and keeping those of a pane's size that face outward; their
     * triangles get vertices of their own marked PART_GLASS, so the shader
     * can make exactly those surfaces glass. */
    uint8_t *vglass = calloc(nv, 1);
    {
        size_t cap_f = 16384, nf = 0;
        uint32_t *fl = malloc(sizeof *fl * cap_f);
        for (size_t t = 0; t < nt; ++t) {
            d3 p0 = pos[vpos[out_idx[t * 3]]], p1 = pos[vpos[out_idx[t * 3 + 1]]], p2 = pos[vpos[out_idx[t * 3 + 2]]];
            double cx = (p0.x + p1.x + p2.x) / 3.0, cy = (p0.y + p1.y + p2.y) / 3.0, cz = (p0.z + p1.z + p2.z) / 3.0;
            if (fabs(cx) < 2.6 && cy > 1.8 && cy < 4.3 && cz > -38.5 && cz < -31.0 &&
                vpart[vpos[out_idx[t * 3]]] == PART_FUSELAGE) {
                if (nf == cap_f) { cap_f *= 2; fl = realloc(fl, sizeof *fl * cap_f); }
                fl[nf++] = (uint32_t)t;
            }
        }
        PEdge *ed = malloc(sizeof *ed * nf * 3);
        for (size_t i = 0; i < nf; ++i)
            for (int k = 0; k < 3; ++k) {
                uint32_t a = vpos[out_idx[fl[i] * 3 + k]], b = vpos[out_idx[fl[i] * 3 + (k + 1) % 3]];
                PEdge e = { a < b ? a : b, a < b ? b : a, (uint32_t)i };
                ed[i * 3 + k] = e;
            }
        qsort(ed, nf * 3, sizeof *ed, cmp_pedge);
        /* neighbours across shared edges (at most 3 each) */
        int32_t *nb = malloc(sizeof *nb * nf * 3);
        for (size_t i = 0; i < nf * 3; ++i) nb[i] = -1;
        for (size_t i = 0; i + 1 < nf * 3; ++i)
            if (ed[i].a == ed[i + 1].a && ed[i].b == ed[i + 1].b) {
                uint32_t f0 = ed[i].f, f1 = ed[i + 1].f;
                for (int k = 0; k < 3; ++k) if (nb[f0 * 3 + k] < 0) { nb[f0 * 3 + k] = (int32_t)f1; break; }
                for (int k = 0; k < 3; ++k) if (nb[f1 * 3 + k] < 0) { nb[f1 * 3 + k] = (int32_t)f0; break; }
            }
        d3 *fnrm = malloc(sizeof *fnrm * nf);
        double *farea = malloc(sizeof *farea * nf);
        d3 *fcen = malloc(sizeof *fcen * nf);
        for (size_t i = 0; i < nf; ++i) {
            d3 p0 = pos[vpos[out_idx[fl[i] * 3]]], p1 = pos[vpos[out_idx[fl[i] * 3 + 1]]], p2 = pos[vpos[out_idx[fl[i] * 3 + 2]]];
            d3 cr = d3cross(d3sub(p1, p0), d3sub(p2, p0));
            farea[i] = 0.5 * sqrt(d3dot(cr, cr));
            fnrm[i] = d3norm(cr);
            d3 c = { (p0.x + p1.x + p2.x) / 3.0, (p0.y + p1.y + p2.y) / 3.0, (p0.z + p1.z + p2.z) / 3.0 };
            fcen[i] = c;
        }
        int32_t *reg = malloc(sizeof *reg * nf);
        for (size_t i = 0; i < nf; ++i) reg[i] = -1;
        uint32_t *stack = malloc(sizeof *stack * nf);
        const double smooth = cos(28.0 * 3.14159265358979 / 180.0);
        int nreg = 0, npane = 0;
        size_t glass_faces = 0;
        uint8_t *is_glass = calloc(nf, 1);
        for (size_t s0 = 0; s0 < nf; ++s0) {
            if (reg[s0] >= 0) continue;
            size_t sp = 0, first_member = 0;
            stack[sp++] = (uint32_t)s0;
            reg[s0] = nreg;
            double area = 0.0;
            d3 an = { 0, 0, 0 }, ac = { 0, 0, 0 };
            /* grow, collecting the members in order behind the stack */
            uint32_t *members = malloc(sizeof *members * nf);
            size_t nm = 0;
            while (sp) {
                uint32_t x = stack[--sp];
                members[nm++] = x;
                area += farea[x];
                an.x += fnrm[x].x * farea[x]; an.y += fnrm[x].y * farea[x]; an.z += fnrm[x].z * farea[x];
                ac.x += fcen[x].x * farea[x]; ac.y += fcen[x].y * farea[x]; ac.z += fcen[x].z * farea[x];
                for (int k = 0; k < 3; ++k) {
                    int32_t y = nb[x * 3 + k];
                    if (y >= 0 && reg[y] < 0 && fabs(d3dot(fnrm[x], fnrm[y])) > smooth) {
                        reg[y] = nreg;
                        stack[sp++] = (uint32_t)y;
                    }
                }
            }
            (void)first_member;
            if (area > 1e-9) {
                d3 c = { ac.x / area, ac.y / area, ac.z / area };
                d3 n = d3norm(an);
                /* outward from the nose's middle */
                d3 out = d3norm((d3){ c.x, c.y - 0.8, c.z + 31.0 });
                int pane = area > 0.08 && area < 4.0 && d3dot(n, out) > 0.55 && c.y > 2.3;
                if (pane) {
                    npane++;
                    fprintf(stderr, "meshpack:   pane %d: %.2f m2 at (%.2f %.2f %.2f), facing (%.2f %.2f %.2f)\n",
                            npane, area, c.x, c.y, c.z, n.x, n.y, n.z);
                    for (size_t m = 0; m < nm; ++m) { is_glass[members[m]] = 1; glass_faces++; }
                }
            }
            free(members);
            nreg++;
        }
        /* the panes' triangles get vertices of their own, marked glass */
        for (size_t i = 0; i < nf; ++i) {
            if (!is_glass[i]) continue;
            for (int k = 0; k < 3; ++k) {
                uint32_t v = out_idx[fl[i] * 3 + k];
                vpos = realloc(vpos, sizeof *vpos * (nv + 1));
                vnrm = realloc(vnrm, sizeof *vnrm * (nv + 1));
                vglass = realloc(vglass, nv + 1);
                vpos[nv] = vpos[v];
                vnrm[nv] = vnrm[v];
                vglass[nv] = 1;
                out_idx[fl[i] * 3 + k] = (uint32_t)nv++;
            }
        }
        fprintf(stderr, "meshpack: %d windscreen panes, %zu triangles of glass (%d smooth regions in the cockpit)\n",
                npane, glass_faces, nreg);
        free(is_glass); free(stack); free(reg); free(fcen); free(farea); free(fnrm); free(nb); free(ed); free(fl);
    }

    /* --- quantise and write ------------------------------------------- */
    d3 lo = { 1e30, 1e30, 1e30 }, hi = { -1e30, -1e30, -1e30 };
    for (size_t i = 0; i < npos; ++i) {
        if (pos[i].x < lo.x) lo.x = pos[i].x;
        if (pos[i].y < lo.y) lo.y = pos[i].y;
        if (pos[i].z < lo.z) lo.z = pos[i].z;
        if (pos[i].x > hi.x) hi.x = pos[i].x;
        if (pos[i].y > hi.y) hi.y = pos[i].y;
        if (pos[i].z > hi.z) hi.z = pos[i].z;
    }
    float center[3] = { (float)(0.5 * (lo.x + hi.x)), (float)(0.5 * (lo.y + hi.y)), (float)(0.5 * (lo.z + hi.z)) };
    float half[3] = { (float)(0.5 * (hi.x - lo.x)), (float)(0.5 * (hi.y - lo.y)), (float)(0.5 * (hi.z - lo.z)) };
    fprintf(stderr, "meshpack: body box x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f m\n",
            lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);

    FILE *o = fopen(argv[2], "wb");
    if (!o) die("cannot write the output");
    uint32_t hdr[2] = { (uint32_t)nv, (uint32_t)(nt * 3) };
    fwrite("MRM1", 1, 4, o);
    fwrite(hdr, 4, 2, o);
    fwrite(center, 4, 3, o);
    fwrite(half, 4, 3, o);
    for (size_t i = 0; i < nv; ++i) {
        d3 p = pos[vpos[i]];
        int16_t q[4];
        double c3[3] = { p.x, p.y, p.z };
        for (int k = 0; k < 3; ++k) {
            double v = (c3[k] - center[k]) / half[k] * 32767.0;
            q[k] = (int16_t)lround(v < -32767 ? -32767 : v > 32767 ? 32767 : v);
        }
        q[3] = vglass[i] ? PART_GLASS : vpart[vpos[i]];
        uint8_t nb[4] = { (uint8_t)(vnrm[i] & 255), (uint8_t)((vnrm[i] >> 8) & 255),
                          (uint8_t)((vnrm[i] >> 16) & 255), 0 };
        fwrite(q, 2, 4, o);
        fwrite(nb, 1, 4, o);
    }
    fwrite(out_idx, 4, nt * 3, o);
    if (fclose(o) != 0) die("write failed");
    fprintf(stderr, "meshpack: wrote %s (%zu bytes)\n", argv[2], 36 + nv * 12 + nt * 12);
    return 0;
}
