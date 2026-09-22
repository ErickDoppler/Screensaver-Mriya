/* Minimal vector/matrix helpers. Column-major 4x4 matrices, OpenGL style.
 * The world is far bigger than a float can hold to the centimetre - a leg of
 * the flight is 1000 km - so positions live in doubles (dvec3) and everything
 * the GPU sees is made relative to the camera first. */
#ifndef MR_MATHX_H
#define MR_MATHX_H
#include <math.h>

#define MR_PI 3.14159265358979323846f
#define MR_PI_D 3.14159265358979323846
#define DEG2RAD(d) ((d) * (MR_PI / 180.f))
#define RAD2DEG(r) ((r) * (180.f / MR_PI))

typedef struct { float x, y, z; } vec3;
typedef struct { double x, y, z; } dvec3;
typedef struct { float m[16]; } mat4;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline double smoothstepd(double e0, double e1, double x) {
    double t = clampd((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
/* Wraps v into [lo, hi) - used for angles that run for hours. */
static inline float wrapf(float v, float lo, float hi) {
    float span = hi - lo;
    if (span <= 0.f) return lo;
    while (v >= hi) v -= span;
    while (v < lo)  v += span;
    return v;
}
static inline double wrapd(double v, double lo, double hi) {
    double span = hi - lo;
    if (span <= 0.0) return lo;
    v = fmod(v - lo, span);
    if (v < 0.0) v += span;
    return v + lo;
}
/* Exponential approach used for frame-rate independent smoothing. */
static inline float approachf(float cur, float target, float tau, float dt) {
    if (tau <= 0.f) return target;
    float a = 1.f - expf(-dt / tau);
    return cur + (target - cur) * a;
}
static inline double approachd(double cur, double target, double tau, double dt) {
    if (tau <= 0.0) return target;
    return cur + (target - cur) * (1.0 - exp(-dt / tau));
}
/* Moves cur towards target by at most rate*dt. */
static inline double rate_limit(double cur, double target, double rate, double dt) {
    double d = target - cur, m = rate * dt;
    return cur + (d > m ? m : (d < -m ? -m : d));
}

static inline vec3 v3(float x, float y, float z) { vec3 r = { x, y, z }; return r; }
static inline vec3 v3_add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3_scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline vec3 v3_cross(vec3 a, vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline vec3 v3_norm(vec3 a) {
    float l = sqrtf(v3_dot(a, a));
    return l > 1e-8f ? v3_scale(a, 1.f / l) : a;
}
static inline vec3 v3_lerp(vec3 a, vec3 b, float t) {
    return v3(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t));
}

static inline dvec3 dv3(double x, double y, double z) { dvec3 r = { x, y, z }; return r; }
static inline dvec3 dv3_add(dvec3 a, dvec3 b) { return dv3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline dvec3 dv3_sub(dvec3 a, dvec3 b) { return dv3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline dvec3 dv3_scale(dvec3 a, double s) { return dv3(a.x * s, a.y * s, a.z * s); }
static inline double dv3_dot(dvec3 a, dvec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline dvec3 dv3_cross(dvec3 a, dvec3 b) {
    return dv3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline dvec3 dv3_norm(dvec3 a) {
    double l = sqrt(dv3_dot(a, a));
    return l > 1e-12 ? dv3_scale(a, 1.0 / l) : a;
}
static inline vec3 dv3_to_v3(dvec3 a) { return v3((float)a.x, (float)a.y, (float)a.z); }
static inline dvec3 v3_to_dv3(vec3 a) { return dv3(a.x, a.y, a.z); }

/* An orthonormal basis: the columns of a rotation matrix. */
typedef struct { vec3 x, y, z; } basis3;
typedef struct { dvec3 x, y, z; } dbasis3;

static inline vec3 basis_apply(basis3 b, vec3 v) {
    return v3(b.x.x * v.x + b.y.x * v.y + b.z.x * v.z,
              b.x.y * v.x + b.y.y * v.y + b.z.y * v.z,
              b.x.z * v.x + b.y.z * v.y + b.z.z * v.z);
}
static inline dvec3 dbasis_apply(dbasis3 b, dvec3 v) {
    return dv3(b.x.x * v.x + b.y.x * v.y + b.z.x * v.z,
               b.x.y * v.x + b.y.y * v.y + b.z.y * v.z,
               b.x.z * v.x + b.y.z * v.y + b.z.z * v.z);
}
/* a * b: b's columns expressed through a. */
static inline basis3 basis_mul(basis3 a, basis3 b) {
    basis3 r = { basis_apply(a, b.x), basis_apply(a, b.y), basis_apply(a, b.z) };
    return r;
}
static inline basis3 dbasis_to_basis(dbasis3 b) {
    basis3 r = { dv3_to_v3(b.x), dv3_to_v3(b.y), dv3_to_v3(b.z) };
    return r;
}
/* Yaw about +y, then pitch about the new +x, in a right-handed y-up frame
 * where -z is forward. Positive yaw turns right, positive pitch looks up. */
static inline basis3 basis_yaw_pitch(float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    vec3 fwd = v3(sy * cp, sp, -cy * cp);
    vec3 right = v3(cy, 0.f, sy);
    vec3 up = v3_cross(right, fwd);
    basis3 r = { right, up, v3_scale(fwd, -1.f) };
    return r;
}
/* Roll about -z (the forward axis): positive rolls the right side down. */
static inline basis3 basis_roll(float roll) {
    float c = cosf(roll), s = sinf(roll);
    basis3 r = { v3(c, -s, 0.f), v3(s, c, 0.f), v3(0.f, 0.f, 1.f) };
    return r;
}

static inline mat4 m4_identity(void) {
    mat4 r = {{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
    return r;
}
static inline mat4 m4_mul(mat4 a, mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static inline mat4 m4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
    mat4 r = {{0}};
    float f = 1.f / tanf(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zfar * znear) / (znear - zfar);
    return r;
}
/* Reversed-nothing, infinite far plane: the terrain runs out past 400 km and
 * the aircraft's nose is half a metre from the lens, so a finite far plane
 * wastes the depth buffer on distance nobody sees the end of. */
static inline mat4 m4_perspective_inf(float fovy_rad, float aspect, float znear) {
    mat4 r = {{0}};
    float f = 1.f / tanf(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = -1.f;
    r.m[11] = -1.f;
    r.m[14] = -2.f * znear;
    return r;
}
static inline mat4 m4_ortho(float l, float r_, float b, float t, float n, float f) {
    mat4 r = m4_identity();
    r.m[0] = 2.f / (r_ - l);
    r.m[5] = 2.f / (t - b);
    r.m[10] = -2.f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}
/* View matrix from a camera basis (x right, y up, z back) at `eye`. */
static inline mat4 m4_view(basis3 b, vec3 eye) {
    mat4 r = m4_identity();
    r.m[0] = b.x.x; r.m[4] = b.x.y; r.m[8]  = b.x.z;
    r.m[1] = b.y.x; r.m[5] = b.y.y; r.m[9]  = b.y.z;
    r.m[2] = b.z.x; r.m[6] = b.z.y; r.m[10] = b.z.z;
    r.m[12] = -v3_dot(b.x, eye);
    r.m[13] = -v3_dot(b.y, eye);
    r.m[14] = -v3_dot(b.z, eye);
    return r;
}
/* Model matrix: rotation by a basis, then translation. */
static inline mat4 m4_model(basis3 b, vec3 t) {
    mat4 r = m4_identity();
    r.m[0] = b.x.x; r.m[1] = b.x.y; r.m[2]  = b.x.z;
    r.m[4] = b.y.x; r.m[5] = b.y.y; r.m[6]  = b.y.z;
    r.m[8] = b.z.x; r.m[9] = b.z.y; r.m[10] = b.z.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
static inline mat4 m4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = v3_norm(v3_sub(center, eye));
    vec3 s = v3_norm(v3_cross(f, up));
    vec3 u = v3_cross(s, f);
    basis3 b = { s, u, v3_scale(f, -1.f) };
    return m4_view(b, eye);
}
static inline vec3 m4_mul_point(mat4 m, vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

/* Cheap deterministic PRNG (xorshift32) so a run can be reproduced. */
typedef struct { unsigned s; } rng_t;
static inline unsigned rng_u32(rng_t *r) {
    unsigned x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}
static inline float rng_f(rng_t *r) { return (rng_u32(r) >> 8) * (1.f / 16777216.f); }
static inline float rng_range(rng_t *r, float a, float b) { return a + (b - a) * rng_f(r); }
static inline int rng_int(rng_t *r, int n) { return n > 0 ? (int)(rng_u32(r) % (unsigned)n) : 0; }
#endif
