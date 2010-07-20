/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * xcb.c: contains all functions which use XCB to talk to X11. Mostly wrappers
 *        around the rather complicated/ugly parts of the XCB API.
 *
 */
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/dpms.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <err.h>

static uint32_t get_colorpixel(char *hex) {
    char strgroups[3][3] = {{hex[0], hex[1], '\0'},
                            {hex[2], hex[3], '\0'},
                            {hex[4], hex[5], '\0'}};
    uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                         (strtol(strgroups[1], NULL, 16)),
                         (strtol(strgroups[2], NULL, 16))};

    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
}       

xcb_visualtype_t *get_root_visual_type(xcb_screen_t *screen) {
    xcb_visualtype_t *visual_type = NULL;
    xcb_depth_iterator_t depth_iter;
    xcb_visualtype_iterator_t visual_iter;

    for (depth_iter = xcb_screen_allowed_depths_iterator(screen);
         depth_iter.rem;
         xcb_depth_next(&depth_iter)) {

        for (visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
             visual_iter.rem;
             xcb_visualtype_next(&visual_iter)) {
            if (screen->root_visual != visual_iter.data->visual_id)
                continue;

            visual_type = visual_iter.data;
            return visual_type;
        }
    }

    return NULL;
}

xcb_window_t open_fullscreen_window(xcb_connection_t *conn, xcb_screen_t *scr, char *color) {
    uint32_t mask = 0;
    uint32_t values[3];
    xcb_window_t win = xcb_generate_id(conn);

    mask |= XCB_CW_BACK_PIXEL;
    values[0] = get_colorpixel(color);

    mask |= XCB_CW_OVERRIDE_REDIRECT;
    values[1] = 1;

    mask |= XCB_CW_EVENT_MASK;
    values[2] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_KEY_RELEASE |
                XCB_EVENT_MASK_VISIBILITY_CHANGE;

    xcb_create_window(conn,
                      24,
                      win, /* the window id */
                      scr->root, /* parent == root */
                      0, 0,
                      scr->width_in_pixels,
                      scr->height_in_pixels, /* dimensions */
                      0, /* border = 0, we draw our own */
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_WINDOW_CLASS_COPY_FROM_PARENT, /* copy visual from parent */
                      mask,
                      values);

    /* Map the window (= make it visible) */
    xcb_map_window(conn, win);

    /* Raise window (put it on top) */
    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);

    return win;
}

/*
 * Returns the mask for Mode_switch (to be used for looking up keysymbols by
 * keycode).
 *
 */
uint32_t get_mod_mask(xcb_connection_t *conn, xcb_key_symbols_t *symbols, uint32_t keycode) {
    xcb_get_modifier_mapping_reply_t *modmap_r;
    xcb_keycode_t *modmap, kc;
    xcb_keycode_t *modeswitchcodes = xcb_key_symbols_get_keycode(symbols, keycode);
    if (modeswitchcodes == NULL)
        return 0;

    modmap_r = xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), NULL);
    modmap = xcb_get_modifier_mapping_keycodes(modmap_r);

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < modmap_r->keycodes_per_modifier; j++) {
            kc = modmap[i * modmap_r->keycodes_per_modifier + j];
            for (xcb_keycode_t *ktest = modeswitchcodes; *ktest; ktest++) {
                if (*ktest != kc)
                    continue;

                free(modeswitchcodes);
                free(modmap_r);
                return (1 << i);
            }
        }

    return 0;
}

void dpms_turn_off_screen(xcb_connection_t *conn) {
    xcb_dpms_enable(conn);
    xcb_dpms_force_level(conn, XCB_DPMS_DPMS_MODE_OFF);
    xcb_flush(conn);
}

/*
 * Repeatedly tries to grab pointer and keyboard (up to 1000 times).
 *
 */
void grab_pointer_and_keyboard(xcb_connection_t *conn, xcb_screen_t *screen) {
    xcb_grab_pointer_cookie_t pcookie;
    xcb_grab_pointer_reply_t *preply;

    xcb_grab_keyboard_cookie_t kcookie;
    xcb_grab_keyboard_reply_t *kreply;

    int tries = 1000;

    while (tries-- > 0) {
        pcookie = xcb_grab_pointer(
            conn,
            false,               /* get all pointer events specified by the following mask */
            screen->root,        /* grab the root window */
            XCB_NONE,            /* which events to let through */
            XCB_GRAB_MODE_ASYNC, /* pointer events should continue as normal */
            XCB_GRAB_MODE_ASYNC, /* keyboard mode */
            XCB_NONE,            /* confine_to = in which window should the cursor stay */
            XCB_NONE,            /* don’t display a special cursor */
            XCB_CURRENT_TIME
        );

        if ((preply = xcb_grab_pointer_reply(conn, pcookie, NULL)) &&
            preply->status == XCB_GRAB_STATUS_SUCCESS) {
            free(preply);
            break;
        }
    }

    while (tries-- > 0) {
        kcookie = xcb_grab_keyboard(
            conn,
            true,                /* report events */
            screen->root,        /* grab the root window */
            XCB_CURRENT_TIME,
            XCB_GRAB_MODE_ASYNC, /* process events as normal, do not require sync */
            XCB_GRAB_MODE_ASYNC
        );

        if ((kreply = xcb_grab_keyboard_reply(conn, kcookie, NULL)) &&
            kreply->status == XCB_GRAB_STATUS_SUCCESS) {
            free(kreply);
            break;
        }
    }

    if (tries == 0)
        errx(EXIT_FAILURE, "Cannot grab pointer/keyboard");
}
