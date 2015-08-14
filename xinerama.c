/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xcb/xinerama.h>

#include "i3lock.h"
#include "xcb.h"
#include "xinerama.h"

/* Number of Xinerama screens which are currently present. */
int xr_screens = 0;

/* The resolutions of the currently present Xinerama screens. */
Rect *xr_resolutions;

static bool xinerama_active;
extern bool debug_mode;

void xinerama_init(void) {
    if (!xcb_get_extension_data(conn, &xcb_xinerama_id)->present) {
        DEBUG("Xinerama extension not found, disabling.\n");
        return;
    }

    xcb_xinerama_is_active_cookie_t cookie;
    xcb_xinerama_is_active_reply_t *reply;

    cookie = xcb_xinerama_is_active(conn);
    reply = xcb_xinerama_is_active_reply(conn, cookie, NULL);
    if (!reply)
        return;

    if (!reply->state) {
        free(reply);
        return;
    }

    xinerama_active = true;
    free(reply);
}

void xinerama_query_screens(void) {
    if (!xinerama_active)
        return;

    xcb_xinerama_query_screens_cookie_t cookie;
    xcb_xinerama_query_screens_reply_t *reply;
    xcb_xinerama_screen_info_t *screen_info;

    cookie = xcb_xinerama_query_screens_unchecked(conn);
    reply = xcb_xinerama_query_screens_reply(conn, cookie, NULL);
    if (!reply) {
        if (debug_mode)
            fprintf(stderr, "Couldn't get Xinerama screens\n");
        return;
    }
    screen_info = xcb_xinerama_query_screens_screen_info(reply);
    int screens = xcb_xinerama_query_screens_screen_info_length(reply);

    Rect *resolutions = malloc(screens * sizeof(Rect));
    /* No memory? Just keep on using the old information. */
    if (!resolutions) {
        free(reply);
        return;
    }
    xr_resolutions = resolutions;
    xr_screens = screens;

    for (int screen = 0; screen < xr_screens; screen++) {
        xr_resolutions[screen].x = screen_info[screen].x_org;
        xr_resolutions[screen].y = screen_info[screen].y_org;
        xr_resolutions[screen].width = screen_info[screen].width;
        xr_resolutions[screen].height = screen_info[screen].height;
        DEBUG("found Xinerama screen: %d x %d at %d x %d\n",
              screen_info[screen].width, screen_info[screen].height,
              screen_info[screen].x_org, screen_info[screen].y_org);
    }

    free(reply);
}
