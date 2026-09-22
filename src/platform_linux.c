/* Linux implementation of platform.h: a small key=value settings file and
 * logging. Everything window-related goes through SDL; XScreenSaver's window
 * is adopted in app.c. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "platform.h"

/* ------------------------------------------------------------------ log -- */
static FILE *g_log;

void plat_log_set_file(const char *path) {
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
}

void plat_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "mriya: %s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ---------------------------------------------------------------- store -- */
/* $XDG_CONFIG_HOME/mriya/settings.conf, one "key=value" per line - the
 * same keys the registry holds on Windows and the command line accepts.
 * The file is read once and rewritten whole on every change; it is a few
 * dozen short lines. */
#define STORE_MAX 128

typedef struct Entry { char key[48]; int value; } Entry;
static Entry g_store[STORE_MAX];
static int   g_store_n;
static int   g_store_loaded;
static char  g_dir[1024];
static char  g_path[1100];

static int store_paths(void) {
    if (g_path[0]) return 1;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)        snprintf(g_dir, sizeof g_dir, "%s/mriya", xdg);
    else if (home && *home) snprintf(g_dir, sizeof g_dir, "%s/.config/mriya", home);
    else return 0;
    snprintf(g_path, sizeof g_path, "%s/settings.conf", g_dir);
    return 1;
}

static Entry *find(const char *key) {
    for (int i = 0; i < g_store_n; ++i)
        if (!strcmp(g_store[i].key, key)) return &g_store[i];
    return NULL;
}

static void store_load(void) {
    if (g_store_loaded) return;
    g_store_loaded = 1;
    if (!store_paths()) return;
    FILE *f = fopen(g_path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f) && g_store_n < STORE_MAX) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq || eq - line >= (long)sizeof g_store[0].key) continue;
        *eq = 0;
        char *end;
        long v = strtol(eq + 1, &end, 0);
        if (end == eq + 1) continue;
        Entry *e = find(line);
        if (!e) {
            e = &g_store[g_store_n++];
            snprintf(e->key, sizeof e->key, "%s", line);
        }
        e->value = (int)v;
    }
    fclose(f);
}

/* mkdir -p for the one or two levels that may be missing */
static int make_dirs(char *path) {
    for (char *p = path + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        int r = mkdir(path, 0755);
        *p = '/';
        if (r != 0 && errno != EEXIST) return 0;
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static int store_flush(void) {
    if (!store_paths()) return 0;
    char dir[sizeof g_dir];
    snprintf(dir, sizeof dir, "%s", g_dir);
    if (!make_dirs(dir)) return 0;
    /* write-then-rename, so a crash never leaves half a settings file */
    char tmp[sizeof g_path + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "# Mriya settings. Same keys as the --<setting> options.\n");
    for (int i = 0; i < g_store_n; ++i)
        fprintf(f, "%s=%d\n", g_store[i].key, g_store[i].value);
    int ok = fclose(f) == 0;
    if (ok) ok = rename(tmp, g_path) == 0;
    if (!ok) remove(tmp);
    return ok;
}

int plat_store_read_int(const char *key, int *out) {
    store_load();
    const Entry *e = find(key);
    if (!e) return 0;
    *out = e->value;
    return 1;
}

/* Strings live in files of their own beside settings.conf. */
int plat_store_read_str(const char *key, char *out, int cap) {
    if (!store_paths()) return 0;
    char path[1100];
    snprintf(path, sizeof path, "%s/%s.txt", g_dir, key);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, cap, f) != NULL;
    fclose(f);
    if (ok) out[strcspn(out, "
")] = 0;
    return ok;
}

int plat_store_write_str(const char *key, const char *value) {
    if (!store_paths()) return 0;
    mkdir(g_dir, 0755);
    char path[1100];
    snprintf(path, sizeof path, "%s/%s.txt", g_dir, key);
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "%s
", value);
    return fclose(f) == 0;
}

/* Screenshots go to ~/Pictures/Mriya; the clipboard is left alone. */
int plat_screenshot_path(char *out, int cap) {
    const char *home = getenv("HOME");
    if (!home || !*home) return 0;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/Pictures", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/Pictures/Mriya", home);
    mkdir(dir, 0755);
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(out, (size_t)cap, "%s/mriya-%04d%02d%02d-%02d%02d%02d.png", dir,
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    return 1;
}

int plat_clipboard_image(const unsigned char *rgba, int w, int h) {
    (void)rgba; (void)w; (void)h;
    return 0;
}

/* HTTPS through curl, which every desktop Linux has. */
int plat_https_get(const char *host, const char *path, char *out, int cap) {
    char cmd[1600];
    for (const char *p = path; *p; ++p) if (*p == ''' || *p == '"' || *p == '`' || *p == '$') return -1;
    snprintf(cmd, sizeof cmd, "curl -s -f -m 15 'https://%s%s'", host, path);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    int n = (int)fread(out, 1, (size_t)cap - 1, f);
    out[n > 0 ? n : 0] = 0;
    return pclose(f) == 0 ? n : -1;
}

int plat_store_write_int(const char *key, int value) {
    store_load();
    Entry *e = find(key);
    if (e && e->value == value) return 1;
    if (!e) {
        if (g_store_n >= STORE_MAX || strlen(key) >= sizeof e->key) return 0;
        e = &g_store[g_store_n++];
        snprintf(e->key, sizeof e->key, "%s", key);
    }
    e->value = value;
    return store_flush();
}
