#include "settings.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_defaults(Settings *s) {
#define X(field, key, def, mn, mx) s->field = def;
    MR_SETTINGS_INT(X)
#undef X
}

void settings_clamp(Settings *s) {
#define X(field, key, def, mn, mx) s->field = clampi(s->field, mn, mx);
    MR_SETTINGS_INT(X)
#undef X
    /* Everything switched off would leave nothing to show. */
    if (s->scene_mask == 0) s->scene_mask = WX_ALL;
    if (s->camera_mask == 0) s->camera_mask = CAM_ALL;
}

void settings_load(Settings *s) {
    int v;
    settings_defaults(s);
#define X(field, key, def, mn, mx) if (plat_store_read_int(key, &v)) s->field = v;
    MR_SETTINGS_INT(X)
#undef X
    settings_clamp(s);
}

void settings_save(const Settings *s) {
#define X(field, key, def, mn, mx) plat_store_write_int(key, s->field);
    MR_SETTINGS_INT(X)
#undef X
}

static const char *const scene_names[WX_COUNT] = {
    "clear", "cumulus", "towering", "thunderstorm", "heavy-rain", "cloud-sea",
    "sunset", "sunrise", "moonlight", "night-storm", "night-deck", "cirrus",
    "snowfall", "haze", "blue-hour", "dust", "aurora", "stratocumulus",
    "altocumulus", "tropical", "between-layers", "alpine", "arctic-day",
    "drizzle", "golden-sea", "stormy-sunset", "himalaya"
};
static const char *const scene_titles[WX_COUNT] = {
    "Clear morning", "Fair-weather cumulus", "Towering cumulus", "Thunderstorm",
    "Heavy rain", "Sea of clouds", "Sunset", "Sunrise over the mountains",
    "Moonlit night", "Night thunderstorm", "Moonlit cloud deck", "Cirrus veil",
    "Snowfall", "Summer haze", "Blue hour", "Desert dust", "Aurora borealis",
    "Broken stratocumulus", "Altocumulus", "Tropical towers", "Between the layers",
    "Alpine", "Arctic day", "Drizzle", "Golden sea", "Stormy sunset", "The Himalaya"
};
static const char *const camera_names[CAM_COUNT] = {
    "nose", "belly", "spine", "fin", "wingtip", "cockpit", "window", "chin",
    "engine", "tailplane", "chase", "wingman", "globe"
};
static const char *const camera_titles[CAM_COUNT] = {
    "On the nose", "Between the engines", "On the spine, looking aft",
    "On top of the fin", "Right wingtip", "Cockpit", "Side window", "Under the chin",
    "Behind the inboard engine", "Tailplane tip", "Chase plane", "Wingman",
    "Around the aircraft"
};

const char *settings_scene_name(int scene) {
    return (scene >= 0 && scene < WX_COUNT) ? scene_names[scene] : "?";
}
const char *settings_scene_title(int scene) {
    return (scene >= 0 && scene < WX_COUNT) ? scene_titles[scene] : "?";
}
int settings_scene_enabled(const Settings *s, int scene) {
    if (scene < 0 || scene >= WX_COUNT) return 0;
    return (s->scene_mask >> scene) & 1;
}
void settings_set_scene(Settings *s, int scene, int on) {
    if (scene < 0 || scene >= WX_COUNT) return;
    if (on) s->scene_mask |= 1 << scene;
    else    s->scene_mask &= ~(1 << scene);
}
const char *settings_camera_name(int cam) {
    return (cam >= 0 && cam < CAM_COUNT) ? camera_names[cam] : "?";
}
const char *settings_camera_title(int cam) {
    return (cam >= 0 && cam < CAM_COUNT) ? camera_titles[cam] : "?";
}
int settings_camera_enabled(const Settings *s, int cam) {
    if (cam < 0 || cam >= CAM_COUNT) return 0;
    return (s->camera_mask >> cam) & 1;
}
void settings_set_camera(Settings *s, int cam, int on) {
    if (cam < 0 || cam >= CAM_COUNT) return;
    if (on) s->camera_mask |= 1 << cam;
    else    s->camera_mask &= ~(1 << cam);
}

/* "--scene-mask sunset,aurora" as well as a raw number. */
static int parse_name_list(const char *val, const char *const *names, int n, int *out) {
    int named = 0, mask = 0;
    for (int i = 0; i < n; ++i) {
        const char *p = val;
        size_t len = strlen(names[i]);
        while ((p = strstr(p, names[i])) != NULL) {
            /* whole word only, so "rain" never matches "heavy-rain" */
            int left_ok = p == val || p[-1] == ',';
            int right_ok = p[len] == 0 || p[len] == ',';
            if (left_ok && right_ok) { mask |= 1 << i; named = 1; break; }
            p += len;
        }
    }
    if (named) *out = mask;
    return named;
}

static int parse_int_value(const char *key, const char *val, int *out) {
    if (!val) return 0;
    if (!strcmp(key, "scene-mask") && parse_name_list(val, scene_names, WX_COUNT, out)) return 1;
    if (!strcmp(key, "camera-mask") && parse_name_list(val, camera_names, CAM_COUNT, out)) return 1;
    if (!strcmp(key, "units")) {
        if (!strcmp(val, "metric"))   { *out = UNITS_METRIC; return 1; }
        if (!strcmp(val, "imperial")) { *out = UNITS_IMPERIAL; return 1; }
    }
    if (!strcmp(key, "hud")) {
        if (!strcmp(val, "captions")) { *out = HUD_CAPTIONS; return 1; }
        if (!strcmp(val, "full"))     { *out = HUD_FULL; return 1; }
    }
    if (!strcmp(val, "on") || !strcmp(val, "true") || !strcmp(val, "yes")) { *out = 1; return 1; }
    if (!strcmp(val, "off") || !strcmp(val, "false") || !strcmp(val, "no")) { *out = 0; return 1; }
    char *end;
    long v = strtol(val, &end, 0);
    if (end == val || *end) return 0;
    *out = (int)v;
    return 1;
}

/* Splits "--name=value" / "-name value" into name and value pointers. */
static const char *split_arg(const char *arg, char *name, size_t name_cap, int *has_inline_value) {
    while (*arg == '-') arg++;
    const char *eq = strchr(arg, '=');
    size_t n = eq ? (size_t)(eq - arg) : strlen(arg);
    name[0] = 0;
    if (n == 0 || n >= name_cap) return NULL;
    memcpy(name, arg, n);
    name[n] = 0;
    *has_inline_value = eq != NULL;
    return eq ? eq + 1 : NULL;
}

int settings_parse_arg(Settings *s, int argc, char **argv, int *i) {
    const char *arg = argv[*i];
    if (arg[0] != '-') return 0;
    char name[64];
    int inline_val = 0;
    const char *val = split_arg(arg, name, sizeof name, &inline_val);
    if (name[0] == 0) return 0;

    int negate = 0;
    const char *key = name;
    if (strncmp(name, "no-", 3) == 0) { negate = 1; key = name + 3; }

#define X(field, key_str, def, mn, mx)                                         \
    if (strcmp(key, key_str) == 0) {                                           \
        if (negate) { s->field = 0; return 1; }                                \
        if ((mx) == 1 && !inline_val &&                                        \
            (*i + 1 >= argc || argv[*i + 1][0] == '-')) { s->field = 1; return 1; } \
        if (!inline_val) { if (*i + 1 >= argc) return 0; val = argv[++*i]; }   \
        int v; if (!parse_int_value(key_str, val, &v)) return 0;               \
        s->field = clampi(v, mn, mx); return 1;                                \
    }
    MR_SETTINGS_INT(X)
#undef X
    return 0;
}
