/* Platform services used by the portable core. Implemented per OS. */
#ifndef MR_PLATFORM_H
#define MR_PLATFORM_H

/* Persistent settings store (Windows: HKCU\Software\Mriya). */
int plat_store_read_int(const char *key, int *out);   /* 1 if the key existed */
int plat_store_write_int(const char *key, int value); /* 1 on success */
int plat_store_read_str(const char *key, char *out, int cap);   /* UTF-8; 1 if it existed */
int plat_store_write_str(const char *key, const char *value);

/* An HTTPS GET of https://host/path into `out` (NUL-terminated). Returns the
 * number of bytes, or -1. Blocking: call it off the main thread. */
int plat_https_get(const char *host, const char *path, char *out, int cap);

/* Screenshots: a new file name in the user's pictures (Pictures/Mriya,
 * created if need be), and the picture onto the clipboard (RGBA, top row
 * first). Return 1 on success. */
int plat_screenshot_path(char *out, int cap);
int plat_clipboard_image(const unsigned char *rgba, int w, int h);

/* Diagnostics. Goes to the debugger/stderr and, if enabled, a log file. */
void plat_log_set_file(const char *path);
void plat_log(const char *fmt, ...);
/* Starts a fresh log of this run in the user's data folder (Windows:
 * %LOCALAPPDATA%\Mriya\last-run.log) unless a log file was already given.
 * Writes the path into out. */
void plat_log_default(char *out, int cap);

#ifdef _WIN32
/* Bounding box of all monitors, in virtual-screen pixels. */
void  plat_win32_virtual_screen(int *x, int *y, int *w, int *h);
/* Creates a child HWND filling `parent` (the little monitor in the Windows
 * screensaver dialog). Returns the child HWND and its size in pixels. */
void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h);
int   plat_win32_window_alive(void *hwnd);
/* Runs the modal settings dialog (the /c mode). Returns 0 when the dialog has
 * dealt with everything, or 1 if it wants the caller to run in-process after
 * it closes (the live preview). */
int   plat_win32_config_dialog(void *parent_hwnd);
#endif
#endif
