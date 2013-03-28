/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010-2012 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "xcb.h"
#include "unlock_indicator.h"
#include "xinerama.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
int input_position;

/* The ev main loop. */
struct ev_loop *main_loop;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

static struct ev_timer *clear_indicator_timeout;

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
pam_state_t pam_state;

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BUTTON_DIAMETER, BUTTON_DIAMETER);
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

    if (unlock_state >= STATE_KEY_PRESSED && unlock_indicator) {
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, 10.0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  0 /* start */,
                  2 * M_PI /* end */);

        /* Use the appropriate color for the different PAM states
         * (currently verifying, wrong password, or default) */
        switch (pam_state) {
            case STATE_PAM_VERIFY:
                cairo_set_source_rgba(ctx, 0, 114.0/255, 255.0/255, 0.75);
                break;
            case STATE_PAM_WRONG:
                cairo_set_source_rgba(ctx, 250.0/255, 0, 0, 0.75);
                break;
            default:
                cairo_set_source_rgba(ctx, 0, 0, 0, 0.75);
                break;
        }
        cairo_fill_preserve(ctx);

        switch (pam_state) {
            case STATE_PAM_VERIFY:
                cairo_set_source_rgb(ctx, 51.0/255, 0, 250.0/255);
                break;
            case STATE_PAM_WRONG:
                cairo_set_source_rgb(ctx, 125.0/255, 51.0/255, 0);
                break;
            case STATE_PAM_IDLE:
                cairo_set_source_rgb(ctx, 51.0/255, 125.0/255, 0);
                break;
        }
        cairo_stroke(ctx);

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

        /* Display a (centered) text of the current PAM state. */
        char *text = NULL;
        switch (pam_state) {
            case STATE_PAM_VERIFY:
                text = "verifying…";
                break;
            case STATE_PAM_WRONG:
                text = "wrong!";
                break;
            default:
                break;
        }

        if (text) {
            cairo_text_extents_t extents;
            double x, y;

            cairo_set_source_rgb(ctx, 0, 0, 0);
            cairo_set_font_size(ctx, 28.0);

            cairo_text_extents(ctx, text, &extents);
            x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
            y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing);

            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, text);
            cairo_close_path(ctx);
        }

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
                cairo_set_source_rgb(ctx, 51.0/255, 219.0/255, 0);
            } else {
                /* For backspace, we use red. */
                cairo_set_source_rgb(ctx, 219.0/255, 51.0/255, 0);
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
                      highlight_start + (M_PI / 3.0) /* start */,
                      (highlight_start + (M_PI / 3.0)) + (M_PI / 128.0) /* end */);
            cairo_stroke(ctx);
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (BUTTON_DIAMETER / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (BUTTON_DIAMETER / 2)));
            cairo_set_source_surface(xcb_ctx, output, x, y);
            cairo_rectangle(xcb_ctx, x, y, BUTTON_DIAMETER, BUTTON_DIAMETER);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (last_resolution[0] / 2) - (BUTTON_DIAMETER / 2);
        int y = (last_resolution[1] / 2) - (BUTTON_DIAMETER / 2);
        cairo_set_source_surface(xcb_ctx, output, x, y);
        cairo_rectangle(xcb_ctx, x, y, BUTTON_DIAMETER, BUTTON_DIAMETER);
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
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){ bg_pixmap });
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
static void clear_indicator(EV_P_ ev_timer *w, int revents) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else unlock_state = STATE_KEY_PRESSED;
    redraw_screen();

    ev_timer_stop(main_loop, clear_indicator_timeout);
    free(clear_indicator_timeout);
    clear_indicator_timeout = NULL;
}

/*
 * (Re-)starts the clear_indicator timeout. Called after pressing backspace or
 * after an unsuccessful authentication attempt.
 *
 */
void start_clear_indicator_timeout(void) {
    if (clear_indicator_timeout) {
        ev_timer_stop(main_loop, clear_indicator_timeout);
        ev_timer_set(clear_indicator_timeout, 1.0, 0.);
        ev_timer_start(main_loop, clear_indicator_timeout);
    } else {
        /* When there is no memory, we just don’t have a timeout. We cannot
         * exit() here, since that would effectively unlock the screen. */
        if (!(clear_indicator_timeout = calloc(sizeof(struct ev_timer), 1)))
            return;
        ev_timer_init(clear_indicator_timeout, clear_indicator, 1.0, 0.);
        ev_timer_start(main_loop, clear_indicator_timeout);
    }
}

/*
 * Stops the clear_indicator timeout.
 *
 */
void stop_clear_indicator_timeout(void) {
    if (clear_indicator_timeout) {
        ev_timer_stop(main_loop, clear_indicator_timeout);
        free(clear_indicator_timeout);
        clear_indicator_timeout = NULL;
    }
}
