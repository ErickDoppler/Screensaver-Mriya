/* OpenGL 3.3 core renderer.
 *
 * Per frame:
 *   1. atmosphere LUTs: transmittance and multiple scattering when the air
 *      changes; the sky view and the aerial-perspective volume every frame
 *   2. the weather map and the cloud-shadow map around the camera
 *   3. the probe: sunlight at the aircraft through the clouds, sky and ground
 *      light, rain at the camera (4 texels, never read back)
 *   4. the aircraft's shadow map
 *   5. the opaque scene into a float target: terrain and sky with a far near
 *      plane, then (depth cleared) the aircraft with a near one - two depth
 *      ranges, because the nose is 20 cm from the lens and the horizon is
 *      400 km off, and one 24-bit buffer cannot hold both
 *   6. the clouds, marched at reduced resolution, stopping at the scene
 *   7. composite, then rain, lightning and the aircraft's lights on top
 *   8. eye adaptation, bloom, tone mapping, lens effects, to the screen
 * Everything offscreen runs at a fraction of the window set by the quality. */
#ifndef MR_RENDER_H
#define MR_RENDER_H
#include "mathx.h"
#include "weather.h"
#include "terrain.h"
#include "model.h"
#include "trails.h"

typedef struct Quality {
    float scale;           /* main render resolution, fraction of the window */
    float cloud_scale;     /* cloud resolution, fraction of the main */
    int   cloud_steps;
    int   shadow_size;
    int   samples;         /* multisampling of the scene: 4, 2 or 1 */
} Quality;

/* Everything the renderer needs to know about the world this frame. */
typedef struct Frame {
    dvec3  cam_pos;
    basis3 cam_basis;
    float  fov, near_plane;
    int    cam_external;
    int    cam_on_airframe;
    dvec3  ac_pos;
    basis3 ac_basis;
    vec3   ac_vel;             /* world, m/s */
    float  flex;
    float  agl;                /* camera height above the ground */
    const Weather      *wx;
    const WeatherState *ws;
    const TerrainParams *tp;
    float  time, dt;
    float  passage;
    float  fade;
    int    nav_lights;
    int    lens;
    float  bloom;
    float  fan_angle, fan_blur;  /* the engines' fans */
    vec3   surf;                 /* aileron, elevator, rudder, radians */
    float  haze;                 /* exhaust shimmer strength */
    const Trails *trails;
    dvec3  wind_off;             /* the air mass's drift: contrails move with it */
} Frame;

typedef struct Renderer {
    /* programs */
    unsigned p_trans, p_multi, p_skyview, p_aerial, p_noise, p_weather, p_cshadow,
             p_probe, p_sky, p_terrain, p_aircraft, p_shadow, p_clouds, p_composite,
             p_precip, p_bolt, p_lights, p_lum, p_adapt, p_bright, p_blur, p_blit, p_final, p_present, p_resolve, p_trail, p_cloudtaa;
    unsigned vao;              /* empty, for fullscreen passes and generated geometry */
    /* the atmosphere */
    unsigned t_trans, f_trans, t_multi, f_multi, t_skyview, f_skyview;
    unsigned t_aerial, t_aerialT, f_aerial;
    float    atmo_key[8];      /* what the transmittance LUT was built for */
    float    atmo_t;           /* when: while the air changes, rebuilt twice a second */
    /* clouds */
    unsigned t_noise_base, t_noise_detail;
    unsigned t_weather, f_weather, t_cshadow, f_cshadow, t_probe, f_probe;
    unsigned t_clouds, t_cdepth, f_clouds;
    unsigned t_hist[2], t_hdepth[2], f_hist[2];   /* accumulated clouds, ping-pong */
    int      hist_idx, hist_valid;
    dvec3    prev_cam;
    basis3   prev_basis;
    float    prev_th, prev_aspect;
    /* the aircraft */
    Model    model;
    Decal    decals[MAX_DECALS];
    int      decal_count;
    unsigned t_shadow, f_shadow;
    int      shadow_size;
    /* terrain */
    unsigned terr_vao, terr_vbo, terr_ibo;
    int      terr_indices;
    /* the scene */
    unsigned f_scene, t_scene, t_dist, rb_depth;
    unsigned f_comp, t_comp;
    unsigned f_ldr, t_ldr;       /* the tone-mapped picture, before the grain */
    /* the scene is drawn multisampled - the edges smooth, the textures sharp -
     * and folded into t_scene / t_dist (msresolve.frag) */
    unsigned f_ms, t_ms_color, t_ms_dist, rb_ms_depth;
    int      samples;
    int      ms_broken;        /* multisampling did not work: single sample */
    int      built_samples;
    int      ldr_w, ldr_h;
    unsigned t_lum, f_lum, t_adapt[2], f_adapt[2];
    int      adapt_idx, adapt_reset, lum_levels;
    unsigned t_bloom[2], f_bloom[2];
    int      bloom_w, bloom_h;
    unsigned bolt_vbo, trail_vbo;
    int      width, height;    /* the window */
    int      w, h;             /* the main target */
    int      cw, ch;           /* the cloud target */
    int      built_w, built_h, built_cw, built_ch;
    Quality  q;
    unsigned frame_index;
    /* GPU timing, as in The Black Hole */
    unsigned gpu_query[3];
    int      query_slot, query_live[3], query_ok;
    float    last_frame_ms;
    /* matrices of this frame, for the HUD */
    mat4     view_proj;
} Renderer;

int  render_init(Renderer *r, int quality);
void render_resize(Renderer *r, int w, int h);
void render_frame(Renderer *r, const Frame *f);
void render_shutdown(Renderer *r);
int  render_dump_png(const Renderer *r, const char *path);
/* Why render_init failed, in words for the user (NULL if it did not). */
const char *render_failure(void);
/* The frame on screen as RGBA, top row first (malloc'd; the caller frees),
 * and a PNG writer for it. */
unsigned char *render_read_rgba(const Renderer *r, int *w, int *h);
int  render_write_png(const char *path, const unsigned char *rgba, int w, int h);
Quality render_quality(int setting);
/* Builds a program from source pieces; "#version 330 core" goes first. */
unsigned render_program(const char *const *vs, int nvs, const char *const *fs, int nfs, const char *name);
/* Projects a world direction to screen uv; returns 0 if behind the camera. */
int  render_project_dir(const Frame *f, float aspect, vec3 dir, float *u, float *v);
#endif
