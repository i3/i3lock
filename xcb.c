/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010-2012 Michael Stapelberg
 *
 * xcb.c: contains all functions which use XCB to talk to X11. Mostly wrappers
 *        around the rather complicated/ugly parts of the XCB API.
 *
 */
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_atom.h>
#include <xcb/dpms.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <err.h>

#include "cursors.h"

xcb_connection_t *conn;
xcb_screen_t *screen;

#define curs_invisible_width 8
#define curs_invisible_height 8

static unsigned char curs_invisible_bits[] = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define curs_windows_width 11
#define curs_windows_height 19

static unsigned char curs_windows_bits[] = {
 0xfe, 0x07, 0xfc, 0x07, 0xfa, 0x07, 0xf6, 0x07, 0xee, 0x07, 0xde, 0x07,
 0xbe, 0x07, 0x7e, 0x07, 0xfe, 0x06, 0xfe, 0x05, 0x3e, 0x00, 0xb6, 0x07,
 0x6a, 0x07, 0x6c, 0x07, 0xde, 0x06, 0xdf, 0x06, 0xbf, 0x05, 0xbf, 0x05,
 0x7f, 0x06 };

#define mask_windows_width 11
#define mask_windows_height 19

static unsigned char mask_windows_bits[] = {
 0x01, 0x00, 0x03, 0x00, 0x07, 0x00, 0x0f, 0x00, 0x1f, 0x00, 0x3f, 0x00,
 0x7f, 0x00, 0xff, 0x00, 0xff, 0x01, 0xff, 0x03, 0xff, 0x07, 0x7f, 0x00,
 0xf7, 0x00, 0xf3, 0x00, 0xe1, 0x01, 0xe0, 0x01, 0xc0, 0x03, 0xc0, 0x03,
 0x80, 0x01 };

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

xcb_pixmap_t create_bg_pixmap(xcb_connection_t *conn, xcb_screen_t *scr, u_int32_t* resolution, char *color) {
    xcb_pixmap_t bg_pixmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, scr->root_depth, bg_pixmap, scr->root,
                      resolution[0], resolution[1]);

    /* Generate a Graphics Context and fill the pixmap with background color
     * (for images that are smaller than your screen) */
    xcb_gcontext_t gc = xcb_generate_id(conn);
    uint32_t values[] = { get_colorpixel(color) };
    xcb_create_gc(conn, gc, bg_pixmap, XCB_GC_FOREGROUND, values);
    xcb_rectangle_t rect = { 0, 0, resolution[0], resolution[1] };
    xcb_poly_fill_rectangle(conn, bg_pixmap, gc, 1, &rect);
    xcb_free_gc(conn, gc);

    return bg_pixmap;
}

xcb_window_t open_fullscreen_window(xcb_connection_t *conn, xcb_screen_t *scr, char *color, xcb_pixmap_t pixmap) {
    uint32_t mask = 0;
    uint32_t values[3];
    xcb_window_t win = xcb_generate_id(conn);

    if (pixmap == XCB_NONE) {
        mask |= XCB_CW_BACK_PIXEL;
        values[0] = get_colorpixel(color);
    } else {
        mask |= XCB_CW_BACK_PIXMAP;
        values[0] = pixmap;
    }

    mask |= XCB_CW_OVERRIDE_REDIRECT;
    values[1] = 1;

    mask |= XCB_CW_EVENT_MASK;
    values[2] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_KEY_RELEASE |
                XCB_EVENT_MASK_VISIBILITY_CHANGE |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    xcb_create_window(conn,
                      XCB_COPY_FROM_PARENT,
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

    char *name = "i3lock";
    xcb_change_property(conn,
                        XCB_PROP_MODE_REPLACE,
                        win,
                        XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING,
                        8,
                        strlen(name),
                        name);

    /* Map the window (= make it visible) */
    xcb_map_window(conn, win);

    /* Raise window (put it on top) */
    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);

    return win;
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
void grab_pointer_and_keyboard(xcb_connection_t *conn, xcb_screen_t *screen, xcb_cursor_t cursor) {
    xcb_grab_pointer_cookie_t pcookie;
    xcb_grab_pointer_reply_t *preply;

    xcb_grab_keyboard_cookie_t kcookie;
    xcb_grab_keyboard_reply_t *kreply;

    int tries = 10000;

    while (tries-- > 0) {
        pcookie = xcb_grab_pointer(
            conn,
            false,               /* get all pointer events specified by the following mask */
            screen->root,        /* grab the root window */
            XCB_NONE,            /* which events to let through */
            XCB_GRAB_MODE_ASYNC, /* pointer events should continue as normal */
            XCB_GRAB_MODE_ASYNC, /* keyboard mode */
            XCB_NONE,            /* confine_to = in which window should the cursor stay */
            cursor,              /* we change the cursor to whatever the user wanted */
            XCB_CURRENT_TIME
        );

        if ((preply = xcb_grab_pointer_reply(conn, pcookie, NULL)) &&
            preply->status == XCB_GRAB_STATUS_SUCCESS) {
            free(preply);
            break;
        }

        /* Make this quite a bit slower */
        usleep(50);
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

        /* Make this quite a bit slower */
        usleep(50);
    }

    if (tries <= 0)
        errx(EXIT_FAILURE, "Cannot grab pointer/keyboard");
}

xcb_cursor_t create_cursor(xcb_connection_t *conn, xcb_screen_t *screen, xcb_window_t win, int choice) {
    xcb_pixmap_t bitmap;
    xcb_pixmap_t mask;
    xcb_cursor_t cursor;

    unsigned char* curs_bits;
    unsigned char* mask_bits;
    int curs_w, curs_h;

    switch (choice) {
        case CURS_NONE:
            curs_bits = curs_invisible_bits;
            mask_bits = curs_invisible_bits;
            curs_w = curs_invisible_width;
            curs_h = curs_invisible_height;
            break;
        case CURS_WIN:
            curs_bits = curs_windows_bits;
            mask_bits = mask_windows_bits;
            curs_w = curs_windows_width;
            curs_h = curs_windows_height;
            break;
        case CURS_DEFAULT:
        default:
            return XCB_NONE; /* XCB_NONE is xcb's way of saying "don't change the cursor" */
    }

    bitmap = xcb_create_pixmap_from_bitmap_data(conn,
                                                win,
                                                curs_bits,
                                                curs_w,
                                                curs_h,
                                                1,
                                                screen->white_pixel,
                                                screen->black_pixel,
                                                NULL);

    mask = xcb_create_pixmap_from_bitmap_data(conn,
                                              win,
                                              mask_bits,
                                              curs_w,
                                              curs_h,
                                              1,
                                              screen->white_pixel,
                                              screen->black_pixel,
                                              NULL);

    cursor = xcb_generate_id(conn);

    xcb_create_cursor(conn,
                      cursor,
                      bitmap,
                      mask,
                      65535,65535,65535,
                      0,0,0,
                      0,0);

    xcb_free_pixmap(conn, bitmap);
    xcb_free_pixmap(conn, mask);

    return cursor;
}
