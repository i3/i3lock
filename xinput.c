/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include "xinput.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include "i3lock.h"
#include "xcb.h"

extern bool debug_mode;

static bool xinput_available = false;

void xinput_init(void) {
    const xcb_query_extension_reply_t *extreply;

    extreply = xcb_get_extension_data(conn, &xcb_input_id);
    if (!extreply->present) {
        DEBUG("xinput is not present\n");
        return;
    }

    DEBUG("xinput: querying version\n");
    xcb_generic_error_t *err = NULL;
    xcb_input_xi_query_version_reply_t *xinput_version =
        xcb_input_xi_query_version_reply(
            conn, xcb_input_xi_query_version(conn, XCB_INPUT_MAJOR_VERSION, XCB_INPUT_MINOR_VERSION), &err);
    if (err != NULL) {
        DEBUG("Could not query xinput version: X11 error code %d\n", err->error_code);
        return;
    }
    DEBUG("xinput %d.%d found\n",
          xinput_version->major_version,
          xinput_version->minor_version);
    free(xinput_version);

    xinput_available = true;
}

bool xinput_grab(xcb_window_t root_window) {
    if (!xinput_available) {
        return true;
    }

    xcb_generic_error_t *err = NULL;
    xcb_input_xi_query_device_reply_t *devices =
        xcb_input_xi_query_device_reply(
            conn, xcb_input_xi_query_device(conn, XCB_INPUT_DEVICE_ALL_MASTER), &err);
    if (err != NULL) {
        DEBUG("xinput: querying devices: X11 error code %d\n", err->error_code);
        return false;
    }

    xcb_input_xi_device_info_iterator_t device_iter;
    for (device_iter = xcb_input_xi_query_device_infos_iterator(devices);
         device_iter.rem;
         xcb_input_xi_device_info_next(&device_iter)) {
        xcb_input_xi_device_info_t *device_info = device_iter.data;
        DEBUG("device %s\n",
              xcb_input_xi_device_info_name(device_info));
        // TODO: skip virtual core pointer/keyboard

        const uint32_t mask =
            XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS |
            XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |
            XCB_INPUT_XI_EVENT_MASK_MOTION |
            XCB_INPUT_XI_EVENT_MASK_ENTER |
            XCB_INPUT_XI_EVENT_MASK_LEAVE;

        xcb_input_xi_grab_device_reply_t *reply =
            xcb_input_xi_grab_device_reply(
                conn,
                xcb_input_xi_grab_device(conn,
                                         root_window,
                                         XCB_TIME_CURRENT_TIME,
                                         XCB_CURSOR_NONE,
                                         device_info->deviceid,
                                         XCB_INPUT_GRAB_MODE_22_ASYNC,
                                         XCB_INPUT_GRAB_MODE_22_ASYNC,
                                         true /* owner_events */,
                                         1,
                                         &mask),
                &err);
        if (err != NULL) {
            DEBUG("xinput: grabbing device %s: X11 error code %d\n",
                  xcb_input_xi_device_info_name(device_info),
                  err->error_code);
            free(devices);
            return false;
        }
	if (reply->status != 0 /* XiGrabSuccess */) {
	  free(reply);
	  free(devices);
	  return false;
	}
	free(reply);
    }

    free(devices);
    return true;
}

void xinput_ungrab(void) {
    if (!xinput_available) {
        return;
    }

    xcb_generic_error_t *err = NULL;
    xcb_input_xi_query_device_reply_t *devices =
        xcb_input_xi_query_device_reply(
            conn, xcb_input_xi_query_device(conn, XCB_INPUT_DEVICE_ALL_MASTER), &err);
    if (err != NULL) {
        DEBUG("xinput: querying devices: X11 error code %d\n", err->error_code);
        return;
    }

    xcb_input_xi_device_info_iterator_t device_iter;
    for (device_iter = xcb_input_xi_query_device_infos_iterator(devices);
         device_iter.rem;
         xcb_input_xi_device_info_next(&device_iter)) {
        xcb_input_xi_device_info_t *device_info = device_iter.data;
        // TODO: skip virtual core pointer/keyboard

        xcb_input_xi_ungrab_device(conn, XCB_TIME_CURRENT_TIME, device_info->deviceid);
    }

    free(devices);
}
