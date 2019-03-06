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
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)
#define INFO_MAXLENGTH 100
#define INFO_TIME_FORMAT "%H:%M"
#define INFO_LOCKTIME_FORMAT "%H:%M"
#define INFO_MARGIN 12


/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

/* When was the computer locked. */
extern struct tm lock_time;

/* tick for timer */
static struct ev_periodic *time_redraw_tick;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

static char *get_login(void) {
    uid_t uid = getuid();
    struct passwd *pwd = getpwuid(uid);
    return pwd ? pwd->pw_name : NULL;
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    const double scaling_factor = get_dpi_value() / 96.0;
    int button_diameter_physical = ceil(scaling_factor * BUTTON_DIAMETER);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor, button_diameter_physical);

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);
    cairo_t *ctx = cairo_create(output);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, 0, 0);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    } else {
        char strgroups[3][3] = {{color[0], color[1], '\0'},
                                {color[2], color[3], '\0'},
                                {color[4], color[5], '\0'}};
        uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                             (strtol(strgroups[1], NULL, 16)),
                             (strtol(strgroups[2], NULL, 16))};
        cairo_set_source_rgb(xcb_ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
        cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(xcb_ctx);
    }

    if (unlock_indicator &&
        (unlock_state >= STATE_KEY_PRESSED || auth_state > STATE_AUTH_IDLE)) {
        cairo_scale(ctx, scaling_factor, scaling_factor);
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, 10.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  0 /* start */,
                  2 * M_PI /* end */);

        /* Display some useful information. */
        /* Time (centered) */
        char buf[INFO_MAXLENGTH];
        memset(buf, 0, sizeof(buf));

        char *text = buf;

        time_t curtime = time(NULL);
        struct tm *tm = localtime(&curtime);

        if (tm->tm_sec >= lock_time.tm_sec)
            tm->tm_sec -= lock_time.tm_sec;
        else
            tm->tm_sec += (60 - lock_time.tm_sec);

        if (tm->tm_hour >= lock_time.tm_hour)
            tm->tm_hour -= lock_time.tm_hour;
        else
            tm->tm_hour += (24 - lock_time.tm_hour);

        if (tm->tm_min >= lock_time.tm_min)
            tm->tm_min -= lock_time.tm_min;
        else
        {
            tm->tm_hour--;
            tm->tm_min += (60 - lock_time.tm_min);
        }

        /* Use the appropriate color for the different PAM states
         * (currently verifying, wrong password, or default) */
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgba(ctx, 0, 114.0 / 255, 255.0 / 255, 0.75);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);
                    break;
                }
                cairo_set_source_rgba(ctx, 0, 0, 0, 0.75);
                break;
        }
        if (tm->tm_hour >= 1)
            cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);

        cairo_fill_preserve(ctx);

        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgb(ctx, 51.0 / 255, 0, 250.0 / 255);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgb(ctx, 125.0 / 255, 51.0 / 255, 0);
                break;
            case STATE_AUTH_IDLE:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgb(ctx, 160.0 / 255, 160.0 / 255, 160.0 / 255);
                    break;
                }

                cairo_set_source_rgb(ctx, 51.0 / 255, 125.0 / 255, 0);
                break;
        }
        if (tm->tm_hour >= 1)
            cairo_set_source_rgb(ctx, 125.0 / 255, 51.0 / 255, 0);

        cairo_stroke(ctx);

        /* set time display */
        strftime(text, 100, INFO_TIME_FORMAT, tm);

        if (tm->tm_hour >= 1)
            cairo_set_source_rgb(ctx, 255, 255, 255);
        else
            cairo_set_source_rgb(ctx, 255, 255, 255);
        cairo_set_font_size(ctx, 32.0);

        cairo_text_extents_t time_extents;
        double time_x, time_y;
        cairo_text_extents(ctx, text, &time_extents);
        time_x = BUTTON_CENTER - ((time_extents.width / 2) + time_extents.x_bearing);
        time_y = BUTTON_CENTER - ((time_extents.height / 2) + time_extents.y_bearing);

        cairo_move_to(ctx, time_x, time_y);
        cairo_show_text(ctx, text);
        cairo_close_path(ctx);

        /* Failed attempts (below) */
        if (failed_attempts == 0) {
            text = get_login();
        } else if (failed_attempts == 1) {
            text = "1 failed attempt";
        } else {
            text = malloc(INFO_MAXLENGTH);
            snprintf(text, INFO_MAXLENGTH, "%i failed attempts.", failed_attempts);
        }

        cairo_set_font_size(ctx, 14.0);

        double x, y;
        cairo_text_extents_t extents;
        cairo_text_extents(ctx, text, &extents);
        x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
        y = time_y - extents.y_bearing + INFO_MARGIN;

        if (show_failed_attempts && failed_attempts > 0) {
            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, text);
            cairo_close_path(ctx);
        }

        if (failed_attempts >= 1)
        {
            text = get_login();

            cairo_text_extents(ctx, text, &extents);
            x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
            y = time_y - time_extents.y_bearing + INFO_MARGIN * 2;

            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, text);
            cairo_close_path(ctx);
        }

        /* Lock time (above) */
        text = "Locked for";

        cairo_text_extents(ctx, text, &extents);
        x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
        y = time_y + time_extents.y_bearing - INFO_MARGIN;

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, text);
        cairo_close_path(ctx);
        cairo_move_to(ctx, BUTTON_CENTER + BUTTON_RADIUS - 5, y - time_extents.y_bearing);

        /* Draw an inner seperator line. */
        cairo_set_source_rgb(ctx, 0, 0, 0);
        cairo_set_line_width(ctx, 2.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS - 5 /* radius */,
                  0,
                  2 * M_PI);
        cairo_stroke(ctx);

        cairo_set_line_width(ctx, 10.0);


        /* After the user pressed any valid key or the backspace key, we
         * highlight a random part of the unlock indicator to confirm this
         * keypress. */
        if (unlock_state == STATE_KEY_ACTIVE ||
            unlock_state == STATE_BACKSPACE_ACTIVE) {
            cairo_new_sub_path(ctx);
            double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start,
                      highlight_start + (M_PI / 3.0));
            if (unlock_state == STATE_KEY_ACTIVE) {
                /* For normal keys, we use a lighter green. */
                cairo_set_source_rgb(ctx, 1, 1, 1);
            } else {
                /* For backspace, we use black. */
                cairo_set_source_rgb(ctx, 0, 0, 0);
            }
            cairo_stroke(ctx);

            /* Draw two little separators for the highlighted part of the
             * unlock indicator. */
            cairo_set_source_rgb(ctx, 0, 0, 0);
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      highlight_start /* start */,
                      highlight_start + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);
            cairo_arc(ctx,
                      BUTTON_CENTER /* x */,
                      BUTTON_CENTER /* y */,
                      BUTTON_RADIUS /* radius */,
                      (highlight_start + (M_PI / 3.0)) - (M_PI / 128.0) /* start */,
                      highlight_start + (M_PI / 3.0) /* end */);
            cairo_stroke(ctx);
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (button_diameter_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (button_diameter_physical / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (button_diameter_physical / 2);
        int y = (last_resolution[1] / 2) - (button_diameter_physical / 2);
        cairo_set_source_surface(xcb_ctx, output, x, y);
        cairo_rectangle(xcb_ctx, x, y, button_diameter_physical, button_diameter_physical);
        cairo_fill(xcb_ctx);
    }

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(output);
    cairo_destroy(ctx);
    cairo_destroy(xcb_ctx);
    return bg_pixmap;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}

/* Periodic redraw for clock */

static void time_redraw_cb(struct ev_loop *loop, ev_periodic *w, int revents) {
    redraw_screen();
}

void start_time_redraw_tick(struct ev_loop* main_loop) {
    if (time_redraw_tick) {
        ev_periodic_set(time_redraw_tick, 1.0, 60., 0);
        ev_periodic_again(main_loop, time_redraw_tick);
    } else {
        if (!(time_redraw_tick = calloc(sizeof(struct ev_periodic), 1)))
            return;
        ev_periodic_init(time_redraw_tick,time_redraw_cb, 1.0, 60., 0);
        ev_periodic_start(main_loop, time_redraw_tick);
    }
}
