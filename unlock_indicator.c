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
#include <xkbcommon/xkbcommon.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
extern int input_position;

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
// START Passcolor...
extern char ivcolor[9];
extern char iwcolor[9];
extern char icolor[9];
extern char rvcolor[9];
extern char rwcolor[9];
extern char rcolor[9];
extern char lcolor[9];
extern char tcolor[9];
extern char khlcolor[9];
extern char bhlcolor[9];
// END Passcolor

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

// START Passcolor...
char strgroupsiv[4][3] = {{ivcolor[0], ivcolor[1], '\0'},
                          {ivcolor[2], ivcolor[3], '\0'},
                          {ivcolor[4], ivcolor[5], '\0'},
                          {ivcolor[6], ivcolor[7], '\0'}};
uint32_t iv16[4] = {(strtol(strgroupsiv[0], NULL, 16)),
                           (strtol(strgroupsiv[1], NULL, 16)),
                           (strtol(strgroupsiv[2], NULL, 16)),
                           (strtol(strgroupsiv[3], NULL, 16))};
char strgroupsiw[4][3] = {{iwcolor[0], iwcolor[1], '\0'},
                          {iwcolor[2], iwcolor[3], '\0'},
                          {iwcolor[4], iwcolor[5], '\0'},
                          {iwcolor[6], iwcolor[7], '\0'}};
uint32_t iw16[4] = {(strtol(strgroupsiw[0], NULL, 16)),
                             (strtol(strgroupsiw[1], NULL, 16)),
                             (strtol(strgroupsiw[2], NULL, 16)),
                             (strtol(strgroupsiw[3], NULL, 16))};
char strgroupsi[4][3] = {{icolor[0], icolor[1], '\0'},
                         {icolor[2], icolor[3], '\0'},
                         {icolor[4], icolor[5], '\0'},
                         {icolor[6], icolor[7], '\0'}};
uint32_t i16[4] = {(strtol(strgroupsi[0], NULL, 16)),
                        (strtol(strgroupsi[1], NULL, 16)),
                        (strtol(strgroupsi[2], NULL, 16)),
                        (strtol(strgroupsi[3], NULL, 16))};
char strgroupsrv[4][3] = {{rvcolor[0], rvcolor[1], '\0'},
                          {rvcolor[2], rvcolor[3], '\0'},
                          {rvcolor[4], rvcolor[5], '\0'},
                          {rvcolor[6], rvcolor[7], '\0'}};
uint32_t rv16[4] = {(strtol(strgroupsrv[0], NULL, 16)),
                         (strtol(strgroupsrv[1], NULL, 16)),
                         (strtol(strgroupsrv[2], NULL, 16)),
                         (strtol(strgroupsrv[3], NULL, 16))};
char strgroupsrw[4][3] = {{rwcolor[0], rwcolor[1], '\0'},
                          {rwcolor[2], rwcolor[3], '\0'},
                          {rwcolor[4], rwcolor[5], '\0'},
                          {rwcolor[6], rwcolor[7], '\0'}};
uint32_t rw16[4] = {(strtol(strgroupsrw[0], NULL, 16)),
                           (strtol(strgroupsrw[1], NULL, 16)),
                           (strtol(strgroupsrw[2], NULL, 16)),
                           (strtol(strgroupsrw[3], NULL, 16))};
char strgroupsr[4][3] = {{rcolor[0], rcolor[1], '\0'},
                         {rcolor[2], rcolor[3], '\0'},
                         {rcolor[4], rcolor[5], '\0'},
                         {rcolor[6], rcolor[7], '\0'}};
uint32_t r16[4] = {(strtol(strgroupsr[0], NULL, 16)),
                      (strtol(strgroupsr[1], NULL, 16)),
                      (strtol(strgroupsr[2], NULL, 16)),
                      (strtol(strgroupsr[3], NULL, 16))};
char strgroupsl[4][3] = {{lcolor[0], lcolor[1], '\0'},
                         {lcolor[2], lcolor[3], '\0'},
                         {lcolor[4], lcolor[5], '\0'},
                         {lcolor[6], lcolor[7], '\0'}};
uint32_t l16[4] = {(strtol(strgroupsl[0], NULL, 16)),
                      (strtol(strgroupsl[1], NULL, 16)),
                      (strtol(strgroupsl[2], NULL, 16)),
                      (strtol(strgroupsl[3], NULL, 16))};
char strgroupst[4][3] = {{tcolor[0], tcolor[1], '\0'},
                         {tcolor[2], tcolor[3], '\0'},
                         {tcolor[4], tcolor[5], '\0'},
                         {tcolor[6], tcolor[7], '\0'}};
uint32_t t16[4] = {(strtol(strgroupst[0], NULL, 16)),
                      (strtol(strgroupst[1], NULL, 16)),
                      (strtol(strgroupst[2], NULL, 16)),
                      (strtol(strgroupst[3], NULL, 16))};
char strgroupsk[4][3] = {{khlcolor[0], khlcolor[1], '\0'},
                         {khlcolor[2], khlcolor[3], '\0'},
                         {khlcolor[4], khlcolor[5], '\0'},
                         {khlcolor[6], khlcolor[7], '\0'}};
uint32_t khl16[4] = {(strtol(strgroupsk[0], NULL, 16)),
                       (strtol(strgroupsk[1], NULL, 16)),
                       (strtol(strgroupsk[2], NULL, 16)),
                       (strtol(strgroupsk[3], NULL, 16))};
char strgroupsb[4][3] = {{bhlcolor[0], bhlcolor[1], '\0'},
                         {bhlcolor[2], bhlcolor[3], '\0'},
                         {bhlcolor[4], bhlcolor[5], '\0'},
                         {bhlcolor[6], bhlcolor[7], '\0'}};
uint32_t bhl16[4] = {(strtol(strgroupsb[0], NULL, 16)),
                      (strtol(strgroupsb[1], NULL, 16)),
                      (strtol(strgroupsb[2], NULL, 16)),
                      (strtol(strgroupsb[3], NULL, 16))};
// END Passcolor

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

/* check_modifier_keys describes the currently active modifiers (Caps Lock, Alt,
   Num Lock or Super) in the modifier_string variable. */
static void check_modifier_keys(void) {
    xkb_mod_index_t idx, num_mods;
    const char *mod_name;

    num_mods = xkb_keymap_num_mods(xkb_keymap);

    for (idx = 0; idx < num_mods; idx++) {
        if (!xkb_state_mod_index_is_active(xkb_state, idx, XKB_STATE_MODS_EFFECTIVE))
            continue;

        mod_name = xkb_keymap_mod_get_name(xkb_keymap, idx);
        if (mod_name == NULL)
            continue;

        /* Replace certain xkb names with nicer, human-readable ones. */
        if (strcmp(mod_name, XKB_MOD_NAME_CAPS) == 0)
            mod_name = "Caps Lock";
        else if (strcmp(mod_name, XKB_MOD_NAME_ALT) == 0)
            mod_name = "Alt";
        else if (strcmp(mod_name, XKB_MOD_NAME_NUM) == 0)
            mod_name = "Num Lock";
        else if (strcmp(mod_name, XKB_MOD_NAME_LOGO) == 0)
            mod_name = "Super";

        char *tmp;
        if (modifier_string == NULL) {
            if (asprintf(&tmp, "%s", mod_name) != -1)
                modifier_string = tmp;
        } else if (asprintf(&tmp, "%s, %s", modifier_string, mod_name) != -1) {
            free(modifier_string);
            modifier_string = tmp;
        }
    }
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
void draw_image(xcb_pixmap_t bg_pixmap, uint32_t *resolution) {
    const double scaling_factor = get_dpi_value() / 96.0;
    int button_diameter_physical = ceil(scaling_factor * BUTTON_DIAMETER);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor, button_diameter_physical);

    if (!vistype)
        vistype = get_root_visual_type(screen);

    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);
    cairo_t *ctx = cairo_create(output);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    /* After the first iteration, the pixmap will still contain the previous
     * contents. Explicitly clear the entire pixmap with the background color
     * first to get back into a defined state: */
    char strgroups[3][3] = {{color[0], color[1], '\0'},
                            {color[2], color[3], '\0'},
                            {color[4], color[5], '\0'}};
    uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                         (strtol(strgroups[1], NULL, 16)),
                         (strtol(strgroups[2], NULL, 16))};
    cairo_set_source_rgb(xcb_ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
    cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
    cairo_fill(xcb_ctx);

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

        /* Use the appropriate color for the different PAM states
         * (currently verifying, wrong password, or default) */
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgba(ctx, (double)iv16[0]/255, (double)iv16[1]/255, (double)iv16[2]/255, (double)iv16[3]/255);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgba(ctx, (double)iw16[0]/255, (double)iw16[1]/255, (double)iw16[2]/255, (double)iw16[3]/255);
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgba(ctx, (double)iw16[0]/255, (double)iw16[1]/255, (double)iw16[2]/255, (double)iw16[3]/255);
                    break;
                }
                cairo_set_source_rgba(ctx, (double)i16[0]/255, (double)i16[1]/255, (double)i16[2]/255, (double)i16[3]/255);
                break;
        }
        cairo_fill_preserve(ctx);

        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgb(ctx, (double)rv16[0]/255, (double)rv16[1]/255, (double)rv16[2]/255, (double)rv16[3]/255);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgb(ctx, (double)rw16[0]/255, (double)rw16[1]/255, (double)rw16[2]/255, (double)rw16[3]/255);
                break;
            case STATE_AUTH_IDLE:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    cairo_set_source_rgb(ctx, (double)rw16[0]/255, (double)rw16[1]/255, (double)rw16[2]/255, (double)rw16[3]/255);
                    break;
                }

                cairo_set_source_rgb(ctx, (double)r16[0]/255, (double)r16[1]/255, (double)r16[2]/255, (double)r16[3]/255);
                break;
        }
        cairo_stroke(ctx);

        /* Draw an inner seperator line. */
        cairo_set_source_rgba(ctx, (double) l16[0]/255, (double) l16[1]/255, (double) l16[2]/255, (double) l16[3]/255);
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
        /* We don't want to show more than a 3-digit number. */
        char buf[4];

        cairo_set_source_rgba(ctx, (double) t16[0]/255, (double) t16[1]/255, (double) t16[2]/255, (double) t16[3]/255);
        cairo_select_font_face(ctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(ctx, 28.0);
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
                text = "Verifying…";
                break;
            case STATE_AUTH_LOCK:
                text = "Locking…";
                break;
            case STATE_AUTH_WRONG:
                text = "Wrong!";
                break;
            case STATE_I3LOCK_LOCK_FAILED:
                text = "Lock failed!";
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    text = "No input";
                }
                if (show_failed_attempts && failed_attempts > 0) {
                    if (failed_attempts > 999) {
                        text = "> 999";
                    } else {
                        snprintf(buf, sizeof(buf), "%d", failed_attempts);
                        text = buf;
                    }
                    cairo_set_source_rgba(ctx, (double) t16[0]/255, (double) t16[1]/255, (double) t16[2]/255, (double) t16[3]/255);
                    cairo_set_font_size(ctx, 32.0);
                }
                break;
        }

        if (text) {
            cairo_text_extents_t extents;
            double x, y;

            cairo_text_extents(ctx, text, &extents);
            x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
            y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing);

            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, text);
            cairo_close_path(ctx);
        }

        if (modifier_string != NULL) {
            cairo_text_extents_t extents;
            double x, y;

            cairo_set_font_size(ctx, 14.0);

            cairo_text_extents(ctx, modifier_string, &extents);
            x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
            y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + 28.0;

            cairo_move_to(ctx, x, y);
            cairo_show_text(ctx, modifier_string);
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
                cairo_set_source_rgba(ctx, (double) khl16[0]/255, (double) khl16[1]/255, (double) khl16[2]/255, (double) khl16[3]/255);
            } else {
                /* For backspace, we use red. */
                cairo_set_source_rgba(ctx, (double) bhl16[0]/255, (double) bhl16[1]/255, (double) bhl16[2]/255, (double) bhl16[3]/255);
            }
            cairo_stroke(ctx);

            /* Draw two little separators for the highlighted part of the
             * unlock indicator. */
            cairo_set_source_rgba(ctx, (double) l16[0]/255, (double) l16[1]/255, (double) l16[2]/255, (double) l16[3]/255);
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
}

static xcb_pixmap_t bg_pixmap = XCB_NONE;

/*
 * Releases the current background pixmap so that the next redraw_screen() call
 * will allocate a new one with the updated resolution.
 *
 */
void free_bg_pixmap(void) {
    xcb_free_pixmap(conn, bg_pixmap);
    bg_pixmap = XCB_NONE;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);

    if (modifier_string) {
        free(modifier_string);
        modifier_string = NULL;
    }
    check_modifier_keys();

    if (bg_pixmap == XCB_NONE) {
        DEBUG("allocating pixmap for %d x %d px\n", last_resolution[0], last_resolution[1]);
        bg_pixmap = create_bg_pixmap(conn, screen, last_resolution, color);
    }

    draw_image(bg_pixmap, last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}
