#ifndef _I3LOCK_H
#define _I3LOCK_H

struct xkb_keymap;
struct xkb_state;
/* This macro will only print debug output when started with --debug.
 * This is important because xautolock (for example) closes stdout/stderr by
 * default, so just printing something to stdout will lead to the data ending
 * up on the X11 socket (!). */
#define DEBUG(fmt, ...)                                   \
    do {                                                  \
        if (debug_mode)                                   \
            printf("[i3lock-debug] " fmt, ##__VA_ARGS__); \
    } while (0)

struct xkb_keymap *xkb_keymap;
struct xkb_state *xkb_state;
#endif
