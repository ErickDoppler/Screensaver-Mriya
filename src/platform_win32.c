/* Windows implementation of platform.h: registry store, the screensaver
 * preview child window, and the settings dialog. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winhttp.h>
#include <shlobj.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "platform.h"
#include "settings.h"
#include "resource.h"
#include "realtime.h"
#include <SDL3/SDL.h>

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
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ------------------------------------------------------------- registry -- */
static const wchar_t *const REG_PATH = L"Software\\Mriya";

static void to_wide(const char *s, wchar_t *out, int cap) {
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
}

int plat_store_read_int(const char *key, int *out) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD type = 0, val = 0, size = sizeof val;
    LSTATUS st = RegQueryValueExW(h, wk, NULL, &type, (BYTE *)&val, &size);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || type != REG_DWORD) return 0;
    *out = (int)val;
    return 1;
}

int plat_store_write_int(const char *key, int value) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS)
        return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD val = (DWORD)value;
    LSTATUS st = RegSetValueExW(h, wk, 0, REG_DWORD, (const BYTE *)&val, sizeof val);
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

int plat_store_read_str(const char *key, char *out, int cap) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    wchar_t wk[128], wv[512];
    to_wide(key, wk, 128);
    DWORD type = 0, size = sizeof wv - sizeof(wchar_t);
    LSTATUS st = RegQueryValueExW(h, wk, NULL, &type, (BYTE *)wv, &size);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || type != REG_SZ) return 0;
    wv[size / sizeof(wchar_t)] = 0;
    WideCharToMultiByte(CP_UTF8, 0, wv, -1, out, cap, NULL, NULL);
    out[cap - 1] = 0;
    return 1;
}

int plat_store_write_str(const char *key, const char *value) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS)
        return 0;
    wchar_t wk[128], wv[512];
    to_wide(key, wk, 128);
    to_wide(value, wv, 512);
    LSTATUS st = RegSetValueExW(h, wk, 0, REG_SZ, (const BYTE *)wv, (DWORD)((wcslen(wv) + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

/* ----------------------------------------------------------- screenshots -- */
int plat_screenshot_path(char *out, int cap) {
    wchar_t dir[MAX_PATH], path[MAX_PATH + 64];
    if (SHGetFolderPathW(NULL, CSIDL_MYPICTURES | CSIDL_FLAG_CREATE, NULL, 0, dir) != S_OK) return 0;
    wcscat(dir, L"\\Mriya");
    CreateDirectoryW(dir, NULL);
    time_t now = time(NULL);
    struct tm lt;
    localtime_s(&lt, &now);
    _snwprintf(path, MAX_PATH + 63, L"%ls\\mriya-%04d%02d%02d-%02d%02d%02d.png", dir,
               lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    path[MAX_PATH + 63] = 0;
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL) > 0;
}

int plat_clipboard_image(const unsigned char *rgba, int w, int h) {
    size_t bytes = (size_t)w * h * 4;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
    if (!mem) return 0;
    unsigned char *p = GlobalLock(mem);
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi;
    bi.biWidth = w;
    bi.biHeight = h;                      /* bottom-up */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = (DWORD)bytes;
    memcpy(p, &bi, sizeof bi);
    unsigned char *px = p + sizeof bi;
    for (int y = 0; y < h; ++y) {
        const unsigned char *src = rgba + (size_t)(h - 1 - y) * w * 4;
        unsigned char *dst = px + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    GlobalUnlock(mem);
    if (!OpenClipboard(NULL)) { GlobalFree(mem); return 0; }
    EmptyClipboard();
    int ok = SetClipboardData(CF_DIB, mem) != NULL;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

/* ----------------------------------------------------------------- https -- */
static int https_try(DWORD access, const wchar_t *whost, const wchar_t *wpath, const char *host,
                     char *out, int cap) {
    int n = -1;
    HINTERNET s = WinHttpOpen(L"Mriya-screensaver/1.0", access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return -1;
    WinHttpSetTimeouts(s, 8000, 8000, 10000, 15000);
    HINTERNET c = WinHttpConnect(s, whost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, len = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        if (code == 200) {
            n = 0;
            DWORD got = 0;
            while (n < cap - 1 && WinHttpReadData(r, out + n, (DWORD)(cap - 1 - n), &got) && got > 0) n += (int)got;
            out[n] = 0;
        } else {
            plat_log("https %s: status %lu", host, (unsigned long)code);
        }
    } else {
        plat_log("https %s: request failed (%lu)", host, (unsigned long)GetLastError());
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return n;
}

/* Straight to the server first; then through the system's proxy settings,
 * for a network that has one. */
int plat_https_get(const char *host, const char *path, char *out, int cap) {
    wchar_t whost[256], wpath[1024];
    to_wide(host, whost, 256);
    to_wide(path, wpath, 1024);
    int n = https_try(WINHTTP_ACCESS_TYPE_NO_PROXY, whost, wpath, host, out, cap);
    if (n < 0) n = https_try(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, whost, wpath, host, out, cap);
    if (n < 0) n = https_try(WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, whost, wpath, host, out, cap);
    return n;
}

/* -------------------------------------------------------------- windows -- */
void plat_win32_virtual_screen(int *x, int *y, int *w, int *h) {
    *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (*w <= 0 || *h <= 0) { *x = 0; *y = 0; *w = 1280; *h = 720; }
}

static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(h, m, wp, lp);
}

void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h) {
    HWND parent = (HWND)parent_hwnd;
    if (!IsWindow(parent)) return NULL;
    RECT rc;
    GetClientRect(parent, &rc);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"MriyaPreview";
    RegisterClassW(&wc);
    HWND child = CreateWindowExW(0, wc.lpszClassName, L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, rc.right, rc.bottom, parent, NULL, wc.hInstance, NULL);
    *w = rc.right > 0 ? rc.right : 1;
    *h = rc.bottom > 0 ? rc.bottom : 1;
    return child;
}

int plat_win32_window_alive(void *hwnd) { return IsWindow((HWND)hwnd) ? 1 : 0; }

/* -------------------------------------------------------- settings dialog -- */
static Settings g_s;
/* The pickers ship hidden. Ctrl+Alt+S reveals them and grows the dialog. */
static int g_pickers_shown;
static int g_done;
static INT_PTR g_result;

/* How much taller the dialog gets, and how far the buttons move, when the
 * pickers are revealed. Dialog units, matching the .rc. */
#define PICKERS_EXTRA_DU 194

static HWND item(HWND dlg, int id) { return GetDlgItem(dlg, id); }

static void set_slider(HWND dlg, int id, int mn, int mx, int pos) {
    HWND t = item(dlg, id);
    int step = (mx - mn) / 10 > 0 ? (mx - mn) / 10 : 1;
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(t, TBM_SETTICFREQ, step, 0);
    SendMessageW(t, TBM_SETPAGESIZE, 0, step);
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
}
static int  get_slider(HWND dlg, int id) { return (int)SendMessageW(item(dlg, id), TBM_GETPOS, 0, 0); }
static void set_check(HWND dlg, int id, int v) { CheckDlgButton(dlg, id, v ? BST_CHECKED : BST_UNCHECKED); }
static int  get_check(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static void set_text(HWND dlg, int id, const wchar_t *text) { SetDlgItemTextW(dlg, id, text); }

static void set_num(HWND dlg, int id, const wchar_t *suffix, int v) {
    wchar_t b[64];
    _snwprintf(b, 63, L"%d%s", v, suffix ? suffix : L"");
    b[63] = 0;
    set_text(dlg, id, b);
}

/* Real time: the place, as found, and what the status line says about it. */
static char g_place[256];

static void rt_status(HWND dlg, const char *msg) {
    wchar_t w[400];
    if (msg) {
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 400);
    } else if (g_place[0]) {
        char b[400];
        double lat = g_s.rt_lat / 1e4, lon = g_s.rt_lon / 1e4;
        snprintf(b, sizeof b, "%s  -  %.2f\xC2\xB0%s, %.2f\xC2\xB0%s   (weather from open-meteo.com; only the place is sent)",
                 g_place, fabs(lat), lat >= 0 ? "N" : "S", fabs(lon), lon >= 0 ? "E" : "W");
        MultiByteToWideChar(CP_UTF8, 0, b, -1, w, 400);
    } else {
        MultiByteToWideChar(CP_UTF8, 0, "Type a city and press Find.", -1, w, 400);
    }
    SetDlgItemTextW(dlg, IDC_RT_PLACE, w);
}

static void rt_find(HWND dlg) {
    wchar_t w[256];
    char q[512], name[256];
    GetDlgItemTextW(dlg, IDC_RT_CITY, w, 256);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, q, sizeof q, NULL, NULL);
    if (!q[0]) { rt_status(dlg, "Type a city first."); return; }
    rt_status(dlg, "Looking it up...");
    UpdateWindow(dlg);
    HCURSOR old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    double lat, lon;
    int ok = realtime_geocode(q, name, sizeof name, &lat, &lon);
    SetCursor(old);
    if (!ok) { rt_status(dlg, "Not found (or no connection). Try the city's name in English."); return; }
    snprintf(g_place, sizeof g_place, "%s", name);
    g_s.rt_lat = (int)lround(lat * 1e4);
    g_s.rt_lon = (int)lround(lon * 1e4);
    MultiByteToWideChar(CP_UTF8, 0, g_place, -1, w, 256);
    SetDlgItemTextW(dlg, IDC_RT_CITY, w);
    rt_status(dlg, NULL);
}

/* The joystick row: its axes can be turned round only if one is plugged in. */
static void joy_detect(HWND dlg) {
    char msg[200] = "No joystick - plug one in to set it up";
    int found = 0;
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks(&n);
        if (ids && n > 0) {
            const char *name = SDL_GetJoystickNameForID(ids[0]);
            snprintf(msg, sizeof msg, "%s", name ? name : "Joystick connected");
            found = 1;
        }
        SDL_free(ids);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
    wchar_t w[200];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 200);
    SetDlgItemTextW(dlg, IDC_JOY_NAME, w);
    static const int ids[] = { IDC_JOY_INV_ROLL, IDC_JOY_INV_PITCH, IDC_JOY_INV_THR, IDC_JOY_INV_RUD };
    for (int i = 0; i < 4; ++i) EnableWindow(GetDlgItem(dlg, ids[i]), found);
}

static void refresh_labels(HWND dlg) {
    set_num(dlg, IDC_SENS_VAL, L" %", g_s.mouse_sensitivity);
    set_num(dlg, IDC_APRES_VAL, L" s", g_s.autopilot_resume);
    set_num(dlg, IDC_LEG_VAL, L" km", g_s.leg_km);
    set_num(dlg, IDC_TURN_VAL, L" km", g_s.turn_km);
    set_num(dlg, IDC_ALTCHG_VAL, L" km", g_s.altitude_change_km);
    set_num(dlg, IDC_TIMESCALE_VAL, L"x", g_s.time_scale);
    set_num(dlg, IDC_MASS_VAL, L" t", g_s.mass_t);
    if (g_s.weather_minutes == 0) set_text(dlg, IDC_WXMIN_VAL, L"never");
    else set_num(dlg, IDC_WXMIN_VAL, L" min", g_s.weather_minutes);
    if (g_s.camera_minutes == 0) set_text(dlg, IDC_CAMMIN_VAL, L"never");
    else set_num(dlg, IDC_CAMMIN_VAL, L" min", g_s.camera_minutes);
    set_num(dlg, IDC_FOV_VAL, L"\x00B0", g_s.fov_deg);
    set_num(dlg, IDC_BLOOM_VAL, L" %", g_s.bloom);
    if (g_s.quality == 0) set_text(dlg, IDC_QUALITY_VAL, L"auto");
    else set_num(dlg, IDC_QUALITY_VAL, L" %", g_s.quality);
    set_num(dlg, IDC_FPS_VAL, L" fps", g_s.target_fps);
}

static void enable_group(HWND dlg, const int *ids, int n, int on) {
    for (int i = 0; i < n; ++i) EnableWindow(item(dlg, ids[i]), on);
}

/* Show/enable rules: "Exit on mouse move" and its sensitivity only mean
 * something when the mouse does not look around; the autopilot delay only
 * when the keys may fly. */
static void refresh_dependencies(HWND dlg) {
    int rot = get_check(dlg, IDC_MOUSEROT);
    static const int mouse_group[] = { IDC_EXITMOUSE, IDC_SENS_LBL, IDC_SENS, IDC_SENS_VAL };
    for (int i = 0; i < 4; ++i) ShowWindow(item(dlg, mouse_group[i]), rot ? SW_HIDE : SW_SHOW);
    static const int sens_group[] = { IDC_SENS_LBL, IDC_SENS, IDC_SENS_VAL };
    enable_group(dlg, sens_group, 3, get_check(dlg, IDC_EXITMOUSE));
    static const int ap_group[] = { IDC_APRES_LBL, IDC_APRES, IDC_APRES_VAL };
    enable_group(dlg, ap_group, 3, get_check(dlg, IDC_MANUAL));
    static const int rt_group[] = { IDC_RT_CITY, IDC_RT_FIND, IDC_RT_PLACE };
    enable_group(dlg, rt_group, 3, get_check(dlg, IDC_RT));
}

static void settings_to_controls(HWND dlg) {
    set_check(dlg, IDC_MOUSEROT, g_s.mouse_rotation);
    set_check(dlg, IDC_EXITMOUSE, g_s.exit_on_mouse_move);
    set_slider(dlg, IDC_SENS, 0, 100, g_s.mouse_sensitivity);
    set_check(dlg, IDC_EXITKEY, g_s.exit_on_any_key);

    set_check(dlg, IDC_MANUAL, g_s.manual_flight);
    set_slider(dlg, IDC_APRES, 5, 600, g_s.autopilot_resume);
    set_slider(dlg, IDC_LEG, 50, 3000, g_s.leg_km);
    set_slider(dlg, IDC_TURN, 10, 100, g_s.turn_km);
    set_slider(dlg, IDC_ALTCHG, 20, 1000, g_s.altitude_change_km);
    set_slider(dlg, IDC_TIMESCALE, 1, 20, g_s.time_scale);
    set_slider(dlg, IDC_MASS, 300, 640, g_s.mass_t);

    set_slider(dlg, IDC_WXMIN, 0, 120, g_s.weather_minutes);
    set_slider(dlg, IDC_CAMMIN, 0, 60, g_s.camera_minutes);
    CheckRadioButton(dlg, IDC_HUD_OFF, IDC_HUD_FULL, IDC_HUD_OFF + g_s.hud);
    CheckRadioButton(dlg, IDC_UNITS_M, IDC_UNITS_IMP, IDC_UNITS_M + g_s.units);
    set_check(dlg, IDC_NAVLIGHTS, g_s.nav_lights);
    set_check(dlg, IDC_LENS, g_s.lens_effects);
    set_check(dlg, IDC_RT, g_s.real_time);
    set_check(dlg, IDC_JOY_INV_ROLL, g_s.joy_inv_roll);
    set_check(dlg, IDC_JOY_INV_PITCH, g_s.joy_inv_pitch);
    set_check(dlg, IDC_JOY_INV_THR, g_s.joy_inv_throttle);
    set_check(dlg, IDC_JOY_INV_RUD, g_s.joy_inv_rudder);
    {
        wchar_t w[256];
        MultiByteToWideChar(CP_UTF8, 0, g_place, -1, w, 256);
        SetDlgItemTextW(dlg, IDC_RT_CITY, w);
        rt_status(dlg, NULL);
    }

    set_slider(dlg, IDC_FOV, 30, 100, g_s.fov_deg);
    set_slider(dlg, IDC_BLOOM, 0, 100, g_s.bloom);
    set_slider(dlg, IDC_QUALITY, 0, 100, g_s.quality);
    set_slider(dlg, IDC_FPS, 10, 120, g_s.target_fps);

    for (int i = 0; i < WX_COUNT; ++i) set_check(dlg, IDC_SCENE_FIRST + i, settings_scene_enabled(&g_s, i));
    for (int i = 0; i < CAM_COUNT; ++i) set_check(dlg, IDC_CAM_FIRST + i, settings_camera_enabled(&g_s, i));
    refresh_labels(dlg);
    refresh_dependencies(dlg);
}

static void controls_to_settings(HWND dlg) {
    g_s.mouse_rotation     = get_check(dlg, IDC_MOUSEROT);
    g_s.exit_on_mouse_move = get_check(dlg, IDC_EXITMOUSE);
    g_s.mouse_sensitivity  = get_slider(dlg, IDC_SENS);
    g_s.exit_on_any_key    = get_check(dlg, IDC_EXITKEY);

    g_s.manual_flight      = get_check(dlg, IDC_MANUAL);
    g_s.autopilot_resume   = get_slider(dlg, IDC_APRES);
    g_s.leg_km             = get_slider(dlg, IDC_LEG);
    g_s.turn_km            = get_slider(dlg, IDC_TURN);
    g_s.altitude_change_km = get_slider(dlg, IDC_ALTCHG);
    g_s.time_scale         = get_slider(dlg, IDC_TIMESCALE);
    g_s.mass_t             = get_slider(dlg, IDC_MASS);

    g_s.weather_minutes    = get_slider(dlg, IDC_WXMIN);
    g_s.camera_minutes     = get_slider(dlg, IDC_CAMMIN);
    g_s.hud = get_check(dlg, IDC_HUD_FULL) ? HUD_FULL : get_check(dlg, IDC_HUD_CAP) ? HUD_CAPTIONS : HUD_OFF;
    g_s.units = get_check(dlg, IDC_UNITS_IMP) ? UNITS_IMPERIAL : UNITS_METRIC;
    g_s.nav_lights         = get_check(dlg, IDC_NAVLIGHTS);
    g_s.lens_effects       = get_check(dlg, IDC_LENS);
    g_s.real_time          = get_check(dlg, IDC_RT);
    g_s.joy_inv_roll       = get_check(dlg, IDC_JOY_INV_ROLL);
    g_s.joy_inv_pitch      = get_check(dlg, IDC_JOY_INV_PITCH);
    g_s.joy_inv_throttle   = get_check(dlg, IDC_JOY_INV_THR);
    g_s.joy_inv_rudder     = get_check(dlg, IDC_JOY_INV_RUD);

    g_s.fov_deg            = get_slider(dlg, IDC_FOV);
    g_s.bloom              = get_slider(dlg, IDC_BLOOM);
    g_s.quality            = get_slider(dlg, IDC_QUALITY);
    g_s.target_fps         = get_slider(dlg, IDC_FPS);

    for (int i = 0; i < WX_COUNT; ++i) settings_set_scene(&g_s, i, get_check(dlg, IDC_SCENE_FIRST + i));
    for (int i = 0; i < CAM_COUNT; ++i) settings_set_camera(&g_s, i, get_check(dlg, IDC_CAM_FIRST + i));
    settings_clamp(&g_s);
}

/* Reveals (or hides again) the pickers, growing the dialog and taking the
 * buttons down with it. */
static void toggle_pickers(HWND dlg) {
    g_pickers_shown = !g_pickers_shown;
    int show = g_pickers_shown ? SW_SHOW : SW_HIDE;
    ShowWindow(item(dlg, IDC_SCENE_BOX), show);
    ShowWindow(item(dlg, IDC_CAM_BOX), show);
    for (int i = 0; i < WX_COUNT; ++i) ShowWindow(item(dlg, IDC_SCENE_FIRST + i), show);
    for (int i = 0; i < CAM_COUNT; ++i) ShowWindow(item(dlg, IDC_CAM_FIRST + i), show);
    ShowWindow(item(dlg, IDC_SCENE_ALL), show);
    ShowWindow(item(dlg, IDC_SCENE_NONE), show);

    RECT du = { 0, 0, 4, PICKERS_EXTRA_DU };
    MapDialogRect(dlg, &du);
    int dy = g_pickers_shown ? du.bottom : -du.bottom;
    static const int buttons[] = { IDC_DEFAULTS, IDC_PREVIEW, IDOK, IDCANCEL };
    for (int i = 0; i < 4; ++i) {
        HWND h = item(dlg, buttons[i]);
        RECT rc;
        GetWindowRect(h, &rc);
        MapWindowPoints(NULL, dlg, (POINT *)&rc, 2);
        SetWindowPos(h, NULL, rc.left, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    RECT wr;
    GetWindowRect(dlg, &wr);
    SetWindowPos(dlg, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top + dy, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(dlg, NULL, TRUE);
}

static void finish(INT_PTR result) {
    g_result = result;
    g_done = 1;
}

/* Ctrl+Alt+S, read from this thread's own keyboard messages. Polling the
 * global keyboard instead (GetAsyncKeyState on a timer) is what keyloggers
 * do, and antivirus heuristics score it as one. */
static int is_picker_chord(const MSG *m) {
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return 0;
    if (m->wParam != 'S' || (m->lParam & (1L << 30))) return 0;
    return (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000);
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        settings_load(&g_s);
        if (!plat_store_read_str("rt-place", g_place, sizeof g_place)) g_place[0] = 0;
        joy_detect(dlg);
        g_pickers_shown = 0;
        settings_to_controls(dlg);
        return TRUE;
    }
    case WM_HSCROLL:
        controls_to_settings(dlg);
        refresh_labels(dlg);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if ((id >= IDC_SCENE_FIRST && id <= IDC_SCENE_LAST) || (id >= IDC_CAM_FIRST && id <= IDC_CAM_LAST)) {
            controls_to_settings(dlg);
            return TRUE;
        }
        switch (id) {
        case IDC_RT_FIND:
            rt_find(dlg);
            return TRUE;
        case IDOK:
            controls_to_settings(dlg);
            settings_save(&g_s);
            plat_store_write_str("rt-place", g_place);
            finish(IDOK);
            return TRUE;
        case IDCANCEL:
            finish(IDCANCEL);
            return TRUE;
        case IDC_DEFAULTS:
            settings_defaults(&g_s);
            settings_to_controls(dlg);
            return TRUE;
        case IDC_PREVIEW:
            /* Save, then hand back to the caller, which runs the preview in
             * this same process: a program relaunching itself looks
             * suspicious to antivirus heuristics. */
            controls_to_settings(dlg);
            settings_save(&g_s);
            plat_store_write_str("rt-place", g_place);
            finish(IDC_PREVIEW);
            return TRUE;
        case IDC_SCENE_ALL:
        case IDC_SCENE_NONE: {
            int on = id == IDC_SCENE_ALL;
            for (int i = 0; i < WX_COUNT; ++i) set_check(dlg, IDC_SCENE_FIRST + i, on);
            controls_to_settings(dlg);
            settings_to_controls(dlg);
            return TRUE;
        }
        default:
            if (HIWORD(wp) == BN_CLICKED) {
                controls_to_settings(dlg);
                refresh_dependencies(dlg);
                return TRUE;
            }
            break;
        }
        break;
    }
    case WM_CLOSE:
        finish(IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

int plat_win32_config_dialog(void *parent_hwnd) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    HWND parent = IsWindow((HWND)parent_hwnd) ? (HWND)parent_hwnd : NULL;
    /* Modal by hand: a modeless dialog with the owner disabled, so the loop
     * below gets to see the keyboard before the focused control does. */
    g_done = 0;
    g_result = IDCANCEL;
    HWND dlg = CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CONFIG), parent, dlg_proc, 0);
    if (!dlg) {
        plat_log("settings dialog failed to open: error %lu", (unsigned long)GetLastError());
        return 0;
    }
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG m;
    int quit = 0;
    while (!g_done) {
        BOOL got = GetMessageW(&m, NULL, 0, 0);
        if (got == 0) { quit = 1; break; }
        if (got == -1) break;
        if (is_picker_chord(&m)) { toggle_pickers(dlg); continue; }
        if (!IsDialogMessageW(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    /* Re-enable the owner before the dialog goes, or Windows hands the
     * activation to some other application's window. */
    if (parent) EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    if (parent) SetForegroundWindow(parent);
    if (quit) PostQuitMessage((int)m.wParam);
    return g_result == IDC_PREVIEW ? 1 : 0;
}

