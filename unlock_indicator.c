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
#include <cairo-ft.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "tinyexpr.h"
#include "fonts.h"

/* clock stuff */
#include <time.h>

extern double circle_radius;
extern double ring_width;

#define BUTTON_RADIUS (circle_radius)
#define RING_WIDTH (ring_width)
// TODO: variable clock/button sizes, I guess
#define BUTTON_SPACE (BUTTON_RADIUS + (RING_WIDTH / 2))
#define BUTTON_CENTER (BUTTON_RADIUS + (RING_WIDTH / 2))
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)
#define CLOCK_WIDTH 400
#define CLOCK_HEIGHT 200

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
extern cairo_surface_t *blur_img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];
/* indicator color options */
extern char insidevercolor[9];
extern char insidewrongcolor[9];
extern char insidecolor[9];
extern char ringvercolor[9];
extern char ringwrongcolor[9];
extern char ringcolor[9];
extern char linecolor[9];
extern char textcolor[9];
extern char layoutcolor[9];
extern char timecolor[9];
extern char datecolor[9];
extern char keyhlcolor[9];
extern char bshlcolor[9];
extern char separatorcolor[9];
extern int internal_line_source;

extern int screen_number;
extern float refresh_rate;

extern bool show_clock;
extern bool always_show_clock;
extern bool show_indicator;
extern int  time_align;
extern int  date_align;
extern int  layout_align;
extern char time_format[32];
extern char date_format[32];
extern char *fonts[5];
extern char ind_x_expr[32];
extern char ind_y_expr[32];
extern char time_x_expr[32];
extern char time_y_expr[32];
extern char date_x_expr[32];
extern char date_y_expr[32];
extern char layout_x_expr[32];
extern char layout_y_expr[32];

extern double time_size;
extern double date_size;
extern double text_size;
extern double modifier_size;
extern double layout_size;

extern char *verif_text;
extern char *wrong_text;
extern char *layout_text;

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* time stuff */
static struct ev_periodic *time_redraw_tick;

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

// color arrays
rgba_t insidever16;
rgba_t insidewrong16;
rgba_t inside16;
rgba_t ringver16;
rgba_t ringwrong16;
rgba_t ring16;
rgba_t line16;
rgba_t text16;
rgba_t layout16;
rgba_t time16;
rgba_t date16;
rgba_t keyhl16;
rgba_t bshl16;
rgba_t sep16;
rgba_t bar16;
// just rgb
rgb_t  rgb16;

// experimental bar stuff

#define BAR_VERT 0
#define BAR_FLAT 1
// experimental bar stuff
extern bool bar_enabled;
extern double *bar_heights;
extern double bar_step;
extern double bar_base_height;
extern double bar_periodic_step;
extern double max_bar_height;
extern double bar_position;
extern int num_bars;
extern int bar_width;
extern int bar_orientation;

extern char bar_base_color[9];
extern char bar_expr[32];
extern bool bar_bidirectional;
extern bool bar_reversed;

static cairo_font_face_t *font_faces[5] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static cairo_font_face_t *get_font_face(int which) {
    if (font_faces[which]) {
        return font_faces[which];
    }
    const unsigned char* face_name = (const unsigned char*) fonts[which];
    FcResult result;
        /*
     * Loads the default config.
     * On successive calls, does no work and just returns true.
     */
    if (!FcInit()) {
        DEBUG("Fontconfig init failed. No text will be shown.\n");
        return NULL;
    }

    /*
     * converts a font face name to a pattern for that face name
     */
    FcPattern *pattern = FcNameParse(face_name);
    if (!pattern) {
        DEBUG("no sans-serif font available\n");
        return NULL;
    }

    /*
     * Gets the default font for our pattern. (Gets the default sans-serif font face)
     * Without these two calls, the FcFontMatch call will fail due to FcConfigGetCurrent()
     * not giving it a valid/useful config.
     */
    FcDefaultSubstitute(pattern);
    if (!FcConfigSubstitute(FcConfigGetCurrent(), pattern, FcMatchPattern)) {
        DEBUG("config sub failed?\n");
        return NULL;
    }

    /*
     * Looks up the font pattern and does some internal RenderPrepare work,
     * then returns the resulting pattern that's ready for rendering.
     */
    FcPattern *pattern_ready = FcFontMatch(FcConfigGetCurrent(), pattern, &result);
    FcPatternDestroy(pattern);
    pattern = NULL;
    if (!pattern_ready) {
        DEBUG("no sans-serif font available\n");
        return NULL;
    }

    /*
     * Passes the given pattern into cairo, which loads it into a cairo freetype font face.
     * Increment its reference count and cache it.
     */
    cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(pattern_ready);
    FcPatternDestroy(pattern_ready);
    font_faces[which] = cairo_font_face_reference(face);
    return face;
}

/*
 * Draws the given text onto the cairo context
 */
// TODO: centering
// TODO: pass in font, font size?
static void draw_text(cairo_t *ctx, const char *text, double x, double y) {
    cairo_text_extents_t extents;

    cairo_text_extents(ctx, text, &extents);
    //x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
    //y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + offset;

    cairo_move_to(ctx, x, y);
    cairo_show_text(ctx, text);
}

/*
 * Initialize all the color arrays once.
 * Called once after options are parsed.
 */

/*
    colorstring: 8-character RGBA string ("ff0000ff", "00000000", "ffffffff", etc)
    colorstring16: array of 4 integers (r, g, b, a).
    MAKE_COLORGROUPS(colorstring, colorstring16) =>

    char colorstring_tmparr[4][3] = {{colorstring[0], colorstring[1], '\0'},
                                     {colorstring[2], colorstring[3], '\0'},
                                     {colorstring[4], colorstring[5], '\0'},
                                     {colorstring[6], colorstring[7], '\0'}};
    uint32_t colorstring16[4] = {(strtol(colorstring_tmparr[0], NULL, 16)),
                                 (strtol(colorstring_tmparr[1], NULL, 16)),
                                 (strtol(colorstring_tmparr[2], NULL, 16)),
                                 (strtol(colorstring_tmparr[3], NULL, 16))};
 */

static void set_color(char* dest, const char* src, int offset) {
    dest[0] = src[offset];
    dest[1] = src[offset + 1];
    dest[2] = '\0';
}

static void colorgen(rgba_str_t* tmp, const char* src, rgba_t* dest) {
    set_color(tmp->red, src, 0);
    set_color(tmp->green, src, 2);
    set_color(tmp->blue, src, 4);
    set_color(tmp->alpha, src, 6);

    dest->red = strtol(tmp->red, NULL, 16) / 255.0;
    dest->green = strtol(tmp->green, NULL, 16) / 255.0;
    dest->blue = strtol(tmp->blue, NULL, 16) / 255.0;
    dest->alpha = strtol(tmp->alpha, NULL, 16) / 255.0;
}

static void colorgen_rgb(rgb_str_t* tmp, const char* src, rgb_t* dest) {
    set_color(tmp->red, src, 0);
    set_color(tmp->green, src, 2);
    set_color(tmp->blue, src, 4);

    dest->red = strtol(tmp->red, NULL, 16) / 255.0;
    dest->green = strtol(tmp->green, NULL, 16) / 255.0;
    dest->blue = strtol(tmp->blue, NULL, 16) / 255.0;
}

void init_colors_once(void) {
    rgba_str_t tmp;
    rgb_str_t  tmp_rgb;

    /* build indicator color arrays */
    colorgen(&tmp, insidevercolor, &insidever16);
    colorgen(&tmp, insidewrongcolor, &insidewrong16);
    colorgen(&tmp, insidecolor, &inside16);
    colorgen(&tmp, ringvercolor, &ringver16);
    colorgen(&tmp, ringwrongcolor, &ringwrong16);
    colorgen(&tmp, ringcolor, &ring16);
    colorgen(&tmp, linecolor, &line16);
    colorgen(&tmp, textcolor, &text16);
    colorgen(&tmp, layoutcolor, &layout16);
    colorgen(&tmp, timecolor, &time16);
    colorgen(&tmp, datecolor, &date16);
    colorgen(&tmp, keyhlcolor, &keyhl16);
    colorgen(&tmp, bshlcolor, &bshl16);
    colorgen(&tmp, separatorcolor, &sep16);
    colorgen(&tmp, bar_base_color, &bar16);
    colorgen_rgb(&tmp_rgb, color, &rgb16);
}

/*
 * Returns the scaling factor of the current screen. E.g., on a 227 DPI MacBook
 * Pro 13" Retina screen, the scaling factor is 227/96 = 2.36.
 *
 */
static double scaling_factor(void) {
    const int dpi = (double) screen->height_in_pixels * 25.4 /
                    (double) screen->height_in_millimeters;
    return (dpi / 96.0);
}


/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    int button_diameter_physical = ceil(scaling_factor() * BUTTON_DIAMETER);
    int clock_width_physical = ceil(scaling_factor() * CLOCK_WIDTH);
    int clock_height_physical = ceil(scaling_factor() * CLOCK_HEIGHT);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          scaling_factor(), button_diameter_physical);

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on.
     * create two more surfaces for time and date display
     */
    cairo_surface_t *output = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, resolution[0], resolution[1]);
    cairo_t *ctx = cairo_create(output);

//    cairo_set_font_face(ctx, get_font_face(0));

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    if (blur_img || img) {
        if (blur_img) {
            cairo_set_source_surface(xcb_ctx, blur_img, 0, 0);
            cairo_paint(xcb_ctx);
        } else { // img can no longer be non-NULL if blur_img is not null
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
    } else {
        cairo_set_source_rgb(xcb_ctx, rgb16.red, rgb16.green, rgb16.blue);
        cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(xcb_ctx);
    }
        
    char *text = NULL;

    if (unlock_indicator &&
        (unlock_state >= STATE_KEY_PRESSED || auth_state > STATE_AUTH_IDLE || show_indicator)) {
        cairo_scale(ctx, scaling_factor(), scaling_factor());
        /* Draw a (centered) circle with transparent background. */
        cairo_set_line_width(ctx, RING_WIDTH);
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
                cairo_set_source_rgba(ctx, insidever16.red, insidever16.green, insidever16.blue, insidever16.alpha);
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgba(ctx, insidewrong16.red, insidewrong16.green, insidewrong16.blue, insidewrong16.alpha);
                break;
            default:
                cairo_set_source_rgba(ctx, inside16.red, inside16.green, inside16.blue, inside16.alpha);
                break;
        }
        cairo_fill_preserve(ctx);

        switch (auth_state) {
            case STATE_AUTH_VERIFY:
            case STATE_AUTH_LOCK:
                cairo_set_source_rgba(ctx, ringver16.red, ringver16.green, ringver16.blue, ringver16.alpha);
                if (internal_line_source == 1) {
                  line16.red = ringver16.red;
                  line16.green = ringver16.green;
                  line16.blue = ringver16.blue;
                  line16.alpha = ringver16.alpha;
                }
                break;
            case STATE_AUTH_WRONG:
            case STATE_I3LOCK_LOCK_FAILED:
                cairo_set_source_rgba(ctx, ringwrong16.red, ringwrong16.green, ringwrong16.blue, ringwrong16.alpha);
                if (internal_line_source == 1) {
                  line16.red = ringwrong16.red;
                  line16.green = ringwrong16.green;
                  line16.blue = ringwrong16.blue;
                  line16.alpha = ringwrong16.alpha;
                }
                break;
            case STATE_AUTH_IDLE:
                cairo_set_source_rgba(ctx, ring16.red, ring16.green, ring16.blue, ring16.alpha);
                if (internal_line_source == 1) {
                  line16.red = ring16.red;
                  line16.green = ring16.green;
                  line16.blue = ring16.blue;
                  line16.alpha = ring16.alpha;
                }
                break;
        }
        cairo_stroke(ctx);

        /* Draw an inner separator line. */
        if (internal_line_source != 2) { //pretty sure this only needs drawn if it's being drawn over the inside?
          cairo_set_source_rgba(ctx, line16.red, line16.green, line16.blue, line16.alpha);
          cairo_set_line_width(ctx, 2.0);
          cairo_arc(ctx,
                    BUTTON_CENTER /* x */,
                    BUTTON_CENTER /* y */,
                    BUTTON_RADIUS - 5 /* radius */,
                    0,
                    2 * M_PI);
          cairo_stroke(ctx);
        }

        cairo_set_line_width(ctx, 10.0);

        /* Display a (centered) text of the current PAM state. */

        /* We don't want to show more than a 3-digit number. */
        char buf[4];

        cairo_set_source_rgba(ctx, text16.red, text16.green, text16.blue, text16.alpha);
        cairo_font_face_t *status_face = get_font_face(WRONG_FONT);
        cairo_set_font_size(ctx, text_size);
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
                text = verif_text;
                status_face = get_font_face(VERIF_FONT);
                break;
            case STATE_AUTH_LOCK:
                text = "locking…";
                status_face = get_font_face(VERIF_FONT);
                break;
            case STATE_AUTH_WRONG:
                text = wrong_text;
                break;
            case STATE_I3LOCK_LOCK_FAILED:
                text = "lock failed!";
                break;
            default:
                if (show_failed_attempts && failed_attempts > 0) {
                    if (failed_attempts > 999) {
                        text = "> 999";
                    } else {
                        snprintf(buf, sizeof(buf), "%d", failed_attempts);
                        text = buf;
                    }
                    cairo_set_font_size(ctx, 32.0);
                }
                break;
        }

        if (auth_state == STATE_AUTH_WRONG && (modifier_string != NULL)) {
            cairo_text_extents_t extents;
            double x, y;

            cairo_set_font_size(ctx, modifier_size);

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
            if (bar_enabled) {
                // note: might be biased to cause more hits on lower indices
                // maybe see about doing ((double) rand() / RAND_MAX) * num_bars
                int index = rand() % num_bars;
                bar_heights[index] = max_bar_height;
                for(int i = 0; i < ((max_bar_height / bar_step) + 1); ++i) {
                    int low_ind = index - i;
                    while (low_ind < 0) {
                        low_ind += num_bars;
                    }
                    int high_ind = (index + i) % num_bars;
                    int tmp_height = max_bar_height - (bar_step * i);
                    if (tmp_height < 0) tmp_height = 0;
                    if (bar_heights[low_ind] < tmp_height)
                        bar_heights[low_ind] = tmp_height;
                    if (bar_heights[high_ind] < tmp_height)
                        bar_heights[high_ind] = tmp_height;
                    if (tmp_height == 0) break;
                }
            } else {
                cairo_set_line_width(ctx, RING_WIDTH);
                cairo_new_sub_path(ctx);
                double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
                cairo_arc(ctx,
                          BUTTON_CENTER /* x */,
                          BUTTON_CENTER /* y */,
                          BUTTON_RADIUS /* radius */,
                          highlight_start,
                          highlight_start + (M_PI / 3.0));
                if (unlock_state == STATE_KEY_ACTIVE) {
                    /* For normal keys, we use a lighter green. */ //lol no
                    cairo_set_source_rgba(ctx, keyhl16.red, keyhl16.green, keyhl16.blue, keyhl16.alpha);
                } else {
                    /* For backspace, we use red. */ //lol no
                    cairo_set_source_rgba(ctx, bshl16.red, bshl16.green, bshl16.blue, bshl16.alpha);
                }

                cairo_stroke(ctx);

                /* Draw two little separators for the highlighted part of the
                 * unlock indicator. */
                cairo_set_source_rgba(ctx, sep16.red, sep16.green, sep16.blue, sep16.alpha);
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
    }
    
    /* https://github.com/ravinrabbid/i3lock-clock/commit/0de3a411fa5249c3a4822612c2d6c476389a1297 */
    // if (show_clock && time)
    
    char time_str[40] = {0};
    char date_str[40] = {0};

    if (show_clock) {
        time_t rawtime;
        struct tm* timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(time_str, 40, time_format, timeinfo);
        strftime(date_str, 40, date_format, timeinfo);
    }

    double ix, iy;
    double x, y;
    double screen_x, screen_y;
    double w, h;
    double tx = 0;
    double ty = 0;
    double dx = 0;
    double dy = 0;

    double clock_width = CLOCK_WIDTH;
    double clock_height = CLOCK_HEIGHT;

    double radius = BUTTON_RADIUS;

    int te_x_err;
    int te_y_err;
    // variable mapping for evaluating the clock position expression
    te_variable vars[] = {
        {"w", &w}, {"h", &h},
        {"x", &screen_x}, {"y", &screen_y},
        {"ix", &ix}, {"iy", &iy},
        {"tx", &tx}, {"ty", &ty},
        {"dx", &dx}, {"dy", &dy},
        {"cw", &clock_width}, {"ch", &clock_height}, // pretty sure this is fine.
        {"r", &radius}
    };
#define NUM_VARS 13

    te_expr *te_ind_x_expr = te_compile(ind_x_expr, vars, NUM_VARS, &te_x_err);
    te_expr *te_ind_y_expr = te_compile(ind_y_expr, vars, NUM_VARS, &te_y_err);
    te_expr *te_time_x_expr = te_compile(time_x_expr, vars, NUM_VARS, &te_x_err);
    te_expr *te_time_y_expr = te_compile(time_y_expr, vars, NUM_VARS, &te_y_err);
    te_expr *te_date_x_expr = te_compile(date_x_expr, vars, NUM_VARS, &te_x_err);
    te_expr *te_date_y_expr = te_compile(date_y_expr, vars, NUM_VARS, &te_y_err);
    te_expr *te_layout_x_expr = te_compile(layout_x_expr, vars, NUM_VARS, &te_x_err);
    te_expr *te_layout_y_expr = te_compile(layout_y_expr, vars, NUM_VARS, &te_y_err);
    te_expr *te_bar_expr = te_compile(bar_expr, vars, NUM_VARS, &te_x_err);

    if (xr_screens > 0 && !bar_enabled) {
        /* Composite the unlock indicator in the middle of each screen. */
        // excuse me, just gonna hack something in right here
        if (screen_number != -1 && screen_number < xr_screens) {
            w = xr_resolutions[screen_number].width;
            h = xr_resolutions[screen_number].height;
            screen_x = xr_resolutions[screen_number].x;
            screen_y = xr_resolutions[screen_number].y;
            if (te_ind_x_expr && te_ind_y_expr) {
                ix = 0;
                iy = 0;
                ix = te_eval(te_ind_x_expr);
                iy = te_eval(te_ind_y_expr);
            }
            else {
                ix = xr_resolutions[screen_number].x + (xr_resolutions[screen_number].width / 2);
                iy = xr_resolutions[screen_number].y + (xr_resolutions[screen_number].height / 2);
            }

            x = ix - (button_diameter_physical / 2);
            y = iy - (button_diameter_physical / 2);

            tx = 0;
            ty = 0;
            tx = te_eval(te_time_x_expr);
            ty = te_eval(te_time_y_expr);
            double time_x = tx;
            double time_y = ty;
            dx = te_eval(te_date_x_expr);
            dy = te_eval(te_date_y_expr);
            double date_x = dx;
            double date_y = dy;
            double layout_x = te_eval(te_layout_x_expr);
            double layout_y = te_eval(te_layout_y_expr);
        } else {
            for (int screen = 0; screen < xr_screens; screen++) {
                w = xr_resolutions[screen].width;
                h = xr_resolutions[screen].height;
                screen_x = xr_resolutions[screen].x;
                screen_y = xr_resolutions[screen].y;
                if (te_ind_x_expr && te_ind_y_expr) {
                    ix = 0;
                    iy = 0;
                    ix = te_eval(te_ind_x_expr);
                    iy = te_eval(te_ind_y_expr);
                }
                else {
                    ix = xr_resolutions[screen].x + (xr_resolutions[screen].width / 2);
                    iy = xr_resolutions[screen].y + (xr_resolutions[screen].height / 2);
                }
                x = ix - (button_diameter_physical / 2);
                y = iy - (button_diameter_physical / 2);
                if (te_time_x_expr && te_time_y_expr) {
                    tx = 0;
                    ty = 0;
                    tx = te_eval(te_time_x_expr);
                    ty = te_eval(te_time_y_expr);
                    double time_x = tx;
                    double time_y = ty;
                    dx = te_eval(te_date_x_expr);
                    dy = te_eval(te_date_y_expr);
                    double date_x = dx;
                    double date_y = dy;
                    double layout_x = te_eval(te_layout_x_expr);
                    double layout_y = te_eval(te_layout_y_expr);
                } else {
                    DEBUG("error codes for exprs are %d, %d\n", te_x_err, te_y_err);
                    DEBUG("exprs: %s, %s\n", time_x_expr, time_y_expr);
                }
            }
        }
    } else if (!bar_enabled) {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        w = last_resolution[0];
        h = last_resolution[1];
        ix = last_resolution[0] / 2;
        iy = last_resolution[1] / 2;
        x = ix - (button_diameter_physical / 2);
        y = iy - (button_diameter_physical / 2);
        if (te_time_x_expr && te_time_y_expr) {
            tx = te_eval(te_time_x_expr);
            ty = te_eval(te_time_y_expr);
            double time_x = tx;
            double time_y = ty;
            dx = te_eval(te_date_x_expr);
            dy = te_eval(te_date_y_expr);
            double date_x = dx;
            double date_y = dy;
            double layout_x = te_eval(te_layout_x_expr);
            double layout_y = te_eval(te_layout_y_expr);
        }
    } else {
        // oh boy, here we go!
        // TODO: get this to play nicely with multiple monitors
        // ideally it'd intelligently span both monitors
        if (screen_number != -1 && screen_number < xr_screens) {
            w = xr_resolutions[screen_number].width;
            h = xr_resolutions[screen_number].height;
            ix = w / 2;
            iy = h / 2;
        } else {
            w = xr_resolutions[0].width;
            h = xr_resolutions[0].height;
            ix = w / 2;
            iy = h / 2;
        }
        double bar_offset = te_eval(te_bar_expr);
        double width, height;
        double back_x = 0, back_y = 0, back_x2 = 0, back_y2 = 0, back_width = 0, back_height = 0;
        for(int i = 0; i < num_bars; ++i) {
            double cur_bar_height = bar_heights[i];

            if (cur_bar_height > 0) {
                if (unlock_state == STATE_BACKSPACE_ACTIVE) {
                    cairo_set_source_rgba(ctx, bshl16.red, bshl16.green, bshl16.blue, bshl16.alpha);
                } else {
                    cairo_set_source_rgba(ctx, keyhl16.red, keyhl16.green, keyhl16.blue, keyhl16.alpha);
                }
            } else {
                switch (auth_state) {
                    case STATE_AUTH_VERIFY:
                    case STATE_AUTH_LOCK:
                        cairo_set_source_rgba(ctx, ringver16.red, ringver16.green, ringver16.blue, ringver16.alpha);
                        break;
                    case STATE_AUTH_WRONG:
                    case STATE_I3LOCK_LOCK_FAILED:
                        cairo_set_source_rgba(ctx, ringwrong16.red, ringwrong16.green, ringwrong16.blue, ringwrong16.alpha);
                        break;
                    default:
                        cairo_set_source_rgba(ctx, bar16.red, bar16.green, bar16.blue, bar16.alpha);
                        break;
                }
            }

            if (bar_orientation == BAR_VERT) {
                width = (cur_bar_height <= 0 ? bar_base_height : cur_bar_height);
                height = bar_width;
                x = bar_offset;
                y = i * bar_width;
                if (bar_reversed) {
                    x -= width;
                }
                else if (bar_bidirectional) {
                    width = (cur_bar_height <= 0 ? bar_base_height : cur_bar_height * 2);
                    x = bar_offset - (width / 2) + (bar_base_height / 2);
                }
            } else {
                width = bar_width;
                height = (cur_bar_height <= 0 ? bar_base_height : cur_bar_height);
                x = i * bar_width;
                y = bar_offset;
                if (bar_reversed) {
                    y -= height;
                }
                else if (bar_bidirectional) {
                    height = (cur_bar_height <= 0 ? bar_base_height : cur_bar_height * 2);
                    y = bar_offset - (height / 2) + (bar_base_height / 2);
                }
            }

            if (cur_bar_height < bar_base_height && cur_bar_height > 0) {
                if (bar_orientation == BAR_VERT) {
                    back_x = bar_offset + cur_bar_height;
                    back_y = y;
                    back_width = bar_base_height - cur_bar_height;
                    back_height = height;
                    if (bar_reversed) {
                       back_x = bar_offset - bar_base_height;
                    }
                    else if (bar_bidirectional) {
                        back_x = bar_offset;
                        back_y2 = y;
                        back_width = (bar_base_height - (cur_bar_height * 2)) / 2;
                        back_x2 = bar_offset + (cur_bar_height * 2) + back_width;
                    }
                } else {
                    back_x = x;
                    back_y = bar_offset + cur_bar_height;
                    back_width = width;
                    back_height = bar_base_height - cur_bar_height;
                    if (bar_reversed) {
                        back_y = bar_offset - bar_base_height;
                    }
                    else if (bar_bidirectional) {
                        back_x2 = x;
                        back_y = bar_offset;
                        back_height = (bar_base_height - (cur_bar_height * 2)) / 2;
                        back_y2= bar_offset + (cur_bar_height * 2) + back_height;
                    }
                }
            }
            cairo_rectangle(ctx, x, y, width, height);
            cairo_fill(ctx);
            switch (auth_state) {
                case STATE_AUTH_VERIFY:
                case STATE_AUTH_LOCK:
                    cairo_set_source_rgba(ctx, ringver16.red, ringver16.green, ringver16.blue, ringver16.alpha);
                    break;
                case STATE_AUTH_WRONG:
                case STATE_I3LOCK_LOCK_FAILED:
                    cairo_set_source_rgba(ctx, ringwrong16.red, ringwrong16.green, ringwrong16.blue, ringwrong16.alpha);
                    break;
                default:
                    cairo_set_source_rgba(ctx, bar16.red, bar16.green, bar16.blue, bar16.alpha);
                    break;
            }

            if (cur_bar_height > 0 && cur_bar_height < bar_base_height &&  ((bar_bidirectional && ((cur_bar_height * 2) < bar_base_height))
                    || (!bar_bidirectional && (cur_bar_height < bar_base_height)))) {
                cairo_rectangle(ctx, back_x, back_y, back_width, back_height);
                cairo_fill(ctx);
                if (bar_bidirectional) {
                    cairo_rectangle(ctx, back_x2, back_y2, back_width, back_height);
                    cairo_fill(ctx);
                }
            }
        }
        for(int i = 0; i < num_bars; ++i) {
            if (bar_heights[i] > 0)
                bar_heights[i] -= bar_periodic_step;
        }
        tx = 0;
        ty = 0;
        tx = te_eval(te_time_x_expr);
        ty = te_eval(te_time_y_expr);
        double time_x = tx;
        double time_y = ty;
        dx = te_eval(te_date_x_expr);
        dy = te_eval(te_date_y_expr);
        double date_x = dx;
        double date_y = dy;
        double layout_x = te_eval(te_layout_x_expr);
        double layout_y = te_eval(te_layout_y_expr);
    }
    // draw text
    cairo_text_extents_t extents;
    if (text) {
        cairo_text_extents(ctx, text, &extents);
        x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
        y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing);

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, text);
        cairo_close_path(ctx);
    }
    // common vars for each if block
    if (*time_str) {
        cairo_set_font_size(ctx, time_size);
        cairo_font_face_t *time_face = get_font_face(TIME_FONT);
        cairo_set_source_rgba(ctx, time16.red, time16.green, time16.blue, time16.alpha);

        cairo_text_extents(ctx, time_str, &extents);
        switch(time_align) {
            case 1:
                x = 0;
                break;
            case 2:
                x = CLOCK_WIDTH - ((extents.width) + extents.x_bearing);
                break;
            case 0:
            default:
                x = CLOCK_WIDTH/2 - ((extents.width / 2) + extents.x_bearing);
                break;
        }

        y = CLOCK_HEIGHT/2;

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, time_str);
        cairo_close_path(ctx);
    }

    if (*date_str) {
        cairo_font_face_t *date_face = get_font_face(DATE_FONT);
        cairo_set_source_rgba(ctx, date16.red, date16.green, date16.blue, date16.alpha);
        cairo_set_font_size(ctx, date_size);

        cairo_text_extents(ctx, date_str, &extents);

        switch(date_align) {
            case 1:
                x = 0;
                break;
            case 2:
                x = CLOCK_WIDTH - ((extents.width) + extents.x_bearing);
                break;
            case 0:
            default:
                x = CLOCK_WIDTH/2 - ((extents.width / 2) + extents.x_bearing);
                break;
        }

        y = CLOCK_HEIGHT/2;

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, date_str);
        cairo_close_path(ctx);
    }
    if (layout_text && *layout_text) {
        cairo_font_face_t *layout_face = get_font_face(LAYOUT_FONT);
        cairo_set_source_rgba(ctx, layout16.red, layout16.green, layout16.blue, layout16.alpha);
        cairo_set_font_size(ctx, layout_size);

        cairo_text_extents(ctx, layout_text, &extents);
        switch(layout_align) {
            case 1:
                x = 0;
                break;
            case 2:
                x = CLOCK_WIDTH - ((extents.width) + extents.x_bearing);
                break;
            case 0:
            default:
                x = CLOCK_WIDTH/2 - ((extents.width / 2) + extents.x_bearing);
                break;
        }

        y = CLOCK_HEIGHT/2;

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, layout_text);
        cairo_close_path(ctx);
    }
        
    cairo_set_source_surface(xcb_ctx, output, 0, 0);
    cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
    cairo_fill(xcb_ctx);

    te_free(te_ind_x_expr);
    te_free(te_ind_y_expr);
    te_free(te_time_x_expr);
    te_free(te_time_y_expr);
    te_free(te_date_x_expr);
    te_free(te_date_y_expr);
    te_free(te_layout_x_expr);
    te_free(te_layout_y_expr);
    te_free(te_bar_expr);

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
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d) @ [%lu]\n", unlock_state, auth_state, (unsigned long)time(NULL));
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
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}

void* start_time_redraw_tick_pthread(void* arg) {
    struct timespec *ts = (struct timespec *) arg;
    while(1) {
        nanosleep(ts, NULL);
        redraw_screen();
    }
    return NULL;
}

static void time_redraw_cb(struct ev_loop *loop, ev_periodic *w, int revents) {
    redraw_screen();
}

void start_time_redraw_tick(struct ev_loop* main_loop) {
    if (time_redraw_tick) {
        ev_periodic_set(time_redraw_tick, 0., refresh_rate, 0);
        ev_periodic_again(main_loop, time_redraw_tick);
    } else {
        if (!(time_redraw_tick = calloc(sizeof(struct ev_periodic), 1))) {
           return;
        }
        ev_periodic_init(time_redraw_tick, time_redraw_cb, 0., refresh_rate, 0);
        ev_periodic_start(main_loop, time_redraw_tick);
    }
}

