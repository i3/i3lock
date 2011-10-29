/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010-2011 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include <xcb/dpms.h>
#include <xcb/xcb_keysyms.h>
#include <err.h>
#include <assert.h>
#include <security/pam_appl.h>
/* FIXME: can we get rid of this header? */
#include <X11/keysym.h>
#include <getopt.h>
#include <string.h>

#ifndef NOLIBCAIRO
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#endif

#include "keysym2ucs.h"
#include "ucs2_to_utf8.h"
#include "xcb.h"
#include "cursors.h"

static xcb_connection_t *conn;
static xcb_cursor_t cursor;
static xcb_key_symbols_t *symbols;
static xcb_screen_t *scr;
static pam_handle_t *pam_handle;
static int input_position = 0;
/* holds the password you enter (in UTF-8) */
static char password[512];
static bool modeswitch_active = false;
static bool iso_level3_shift_active = false;
static int modeswitchmask;
static int numlockmask;
static bool beep = false;

#ifndef NOLIBCAIRO
static cairo_surface_t *img = NULL;
static bool tile = false;
#endif

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(xcb_visualtype_t *vistype, u_int32_t* resolution, char* color) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;

#ifndef NOLIBCAIRO
    if (!img)
        return bg_pixmap;

    bg_pixmap = create_bg_pixmap(conn, scr, resolution, color);
    /* Initialize cairo */
    cairo_surface_t *output;
    output = cairo_xcb_surface_create(conn, bg_pixmap, vistype,
             resolution[0], resolution[1]);
    cairo_t *ctx = cairo_create(output);
    if (!tile) {
        cairo_set_source_surface(ctx, img, 0, 0);
        cairo_paint(ctx);
    } else {
        /* create a pattern and fill a rectangle as big as the screen */
        cairo_pattern_t *pattern;
        pattern = cairo_pattern_create_for_surface(img);
        cairo_set_source(ctx, pattern);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
        cairo_rectangle(ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(ctx);
        cairo_pattern_destroy(pattern);
    }
    cairo_surface_destroy(output);
    cairo_destroy(ctx);
#endif
    return bg_pixmap;
}

static void input_done() {
    if (input_position == 0)
        return;

    /* TODO: change cursor during authentication? */
    if (pam_authenticate(pam_handle, 0) == PAM_SUCCESS) {
        printf("successfully authenticated\n");
        exit(0);
    }

    fprintf(stderr, "Authentication failure\n");

    /* beep on authentication failure, if enabled */
    if (beep) {
        xcb_bell(conn, 100);
        xcb_flush(conn);
    }
}

/*
 * Called when the user releases a key. We need to leave the Mode_switch
 * state when the user releases the Mode_switch key.
 *
 */
static void handle_key_release(xcb_key_release_event_t *event) {
    //printf("releasing %d, state raw = %d\n", event->detail, event->state);

    /* fix state */
    event->state &= ~numlockmask;

    xcb_keysym_t sym = xcb_key_press_lookup_keysym(symbols, event, event->state);
    if (sym == XK_Mode_switch) {
        //printf("Mode switch disabled\n");
        modeswitch_active = false;
    } else if (sym == XK_ISO_Level3_Shift) {
        iso_level3_shift_active = false;
    }
}

/*
 * Handle key presses. Fixes state, then looks up the key symbol for the
 * given keycode, then looks up the key symbol (as UCS-2), converts it to
 * UTF-8 and stores it in the password array.
 *
 */
static void handle_key_press(xcb_key_press_event_t *event) {
    //printf("keypress %d, state raw = %d\n", event->detail, event->state);

    xcb_keysym_t sym0, sym1, sym;
    /* For each keycode, there is a list of symbols. The list could look like this:
     * $ xmodmap -pke | grep 'keycode  38'
     * keycode  38 = a A adiaeresis Adiaeresis o O
     * In non-X11 terminology, the symbols for the keycode 38 (the key labeled
     * with "a" on my keyboard) are "a A ä Ä o O".
     * Another form to display the same information is using xkbcomp:
     * $ xkbcomp $DISPLAY /tmp/xkb.dump
     * Then open /tmp/xkb.dump and search for '\<a\>' (in VIM regexp-language):
     *
     * symbols[Group1]= [               a,               A,               o,               O ],
     * symbols[Group2]= [      adiaeresis,      Adiaeresis ]
     *
     * So there are two *groups*, one containing 'a A' and one containing 'ä
     * Ä'. You can use Mode_switch to switch between these groups. You can use
     * ISO_Level3_Shift to reach the 'o O' part of the first group (it’s the
     * same group, just an even higher shift level).
     *
     * So, using the "logical" XKB information, the following lookup will be
     * performed:
     *
     * Neither Mode_switch nor ISO_Level3_Shift active: group 1, column 0 and 1
     * Mode_switch active: group 2, column 0 and 1
     * ISO_Level3_Shift active: group 1, column 2 and 3
     *
     * Using the column index which xcb_key_press_lookup_keysym uses (and
     * xmodmap prints out), the following lookup will be performed:
     *
     * Neither Mode_switch nor ISO_Level3_Shift active: column 0 and 1
     * Mode_switch active: column 2 and 3
     * ISO_Level3_Shift active: column 4 and 5
     */
    int base_column = 0;
    if (modeswitch_active)
        base_column = 2;
    if (iso_level3_shift_active)
        base_column = 4;
    sym0 = xcb_key_press_lookup_keysym(symbols, event, base_column);
    sym1 = xcb_key_press_lookup_keysym(symbols, event, base_column + 1);
    switch (sym0) {
    case XK_Mode_switch:
        //printf("Mode switch enabled\n");
        modeswitch_active = true;
        return;
    case XK_ISO_Level3_Shift:
        DEBUG("ISO_Level3_Shift enabled\n");
        iso_level3_shift_active = true;
        return;
    case XK_Return:
    case XK_KP_Enter:
        input_done();
    case XK_Escape:
        input_position = 0;
        password[input_position] = '\0';
        return;

    case XK_BackSpace:
        if (input_position == 0)
            return;

        /* decrement input_position to point to the previous glyph */
        u8_dec(password, &input_position);
        password[input_position] = '\0';
        //printf("new input position = %d, new password = %s\n", input_position, password);
        return;
    }

    if ((input_position + 8) >= sizeof(password))
        return;

    if ((event->state & numlockmask) && xcb_is_keypad_key(sym1)) {
        /* this key was a keypad key */
        if ((event->state & XCB_MOD_MASK_SHIFT))
            sym = sym0;
        else sym = sym1;
    } else {
        if ((event->state & XCB_MOD_MASK_SHIFT))
            sym = sym1;
        else sym = sym0;
    }

#if 0
    /* FIXME: handle all of these? */
    printf("is_keypad_key = %d\n", xcb_is_keypad_key(sym));
    printf("is_private_keypad_key = %d\n", xcb_is_private_keypad_key(sym));
    printf("xcb_is_cursor_key = %d\n", xcb_is_cursor_key(sym));
    printf("xcb_is_pf_key = %d\n", xcb_is_pf_key(sym));
    printf("xcb_is_function_key = %d\n", xcb_is_function_key(sym));
    printf("xcb_is_misc_function_key = %d\n", xcb_is_misc_function_key(sym));
    printf("xcb_is_modifier_key = %d\n", xcb_is_modifier_key(sym));
#endif

    if (xcb_is_modifier_key(sym) || xcb_is_cursor_key(sym))
        return;

    //printf("sym = %c (%d)\n", sym, sym);

    /* convert the keysym to UCS */
    uint16_t ucs = keysym2ucs(sym);
    if ((int16_t)ucs == -1) {
        fprintf(stderr, "Keysym could not be converted to UCS, skipping\n");
        return;
    }

    /* store the UCS in a string to convert it */
    uint8_t inp[3] = {(ucs & 0xFF00) >> 8, (ucs & 0xFF), 0};

    /* store it in the password array as UTF-8 */
    input_position += convert_ucs_to_utf8((char*)inp, password + input_position);
    password[input_position] = '\0';
    //printf("current password = %s\n", password);
}

/*
 * A visibility notify event will be received when the visibility (= can the
 * user view the complete window) changes, so for example when a popup overlays
 * some area of the i3lock window.
 *
 * In this case, we raise our window on top so that the popup (or whatever is
 * hiding us) gets hidden.
 *
 */
static void handle_visibility_notify(xcb_visibility_notify_event_t *event) {
    if (event->state != XCB_VISIBILITY_UNOBSCURED) {
        uint32_t values[] = { XCB_STACK_MODE_ABOVE };
        xcb_configure_window(conn, event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
        xcb_flush(conn);
    }
}

/*
 * Called when the keyboard mapping changes. We update our symbols.
 *
 */
static void handle_mapping_notify(xcb_mapping_notify_event_t *event) {
    xcb_refresh_keyboard_mapping(symbols, event);

    modeswitchmask = get_mod_mask(conn, symbols, XK_Mode_switch);
    numlockmask = get_mod_mask(conn, symbols, XK_Num_Lock);
}

/*
 * Called when the properties on the root window change, e.g. when the screen
 * resolution changes. If so we update the window to cover the whole screen
 * and also redraw the image, if any.
 *
 */
void handle_screen_resize(xcb_visualtype_t *vistype, xcb_window_t win, uint32_t* last_resolution, char* color) {
    xcb_get_geometry_cookie_t geomc;
    xcb_get_geometry_reply_t *geom;
    geomc = xcb_get_geometry(conn, scr->root);
    if ((geom = xcb_get_geometry_reply(conn, geomc, 0)) == NULL) {
      return;
    }

    if (last_resolution[0] == geom->width && last_resolution[1] == geom->height)
      return;

    last_resolution[0] = geom->width;
    last_resolution[1] = geom->height;

#ifndef NOLIBCAIRO
    if (img) {
        xcb_pixmap_t bg_pixmap = draw_image(vistype, last_resolution, color);
        xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){ bg_pixmap });
    }
#endif

    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(conn, win, mask, last_resolution);
    xcb_flush(conn);
}

/*
 * Callback function for PAM. We only react on password request callbacks.
 *
 */
static int conv_callback(int num_msg, const struct pam_message **msg,
                         struct pam_response **resp, void *appdata_ptr)
{
    if (num_msg == 0)
        return 1;

    /* PAM expects an array of responses, one for each message */
    if ((*resp = calloc(num_msg, sizeof(struct pam_message))) == NULL) {
        perror("calloc");
        return 1;
    }

    for (int c = 0; c < num_msg; c++) {
        if (msg[c]->msg_style != PAM_PROMPT_ECHO_OFF &&
            msg[c]->msg_style != PAM_PROMPT_ECHO_ON)
            continue;

        /* return code is currently not used but should be set to zero */
        resp[c]->resp_retcode = 0;
        if ((resp[c]->resp = strdup(password)) == NULL) {
            perror("strdup");
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    bool dont_fork = false;
    bool dpms = false;
    char color[7] = "ffffff";
    char *username;
#ifndef NOLIBCAIRO
    char *image_path = NULL;
#endif
    int ret;
    struct pam_conv conv = {conv_callback, NULL};
    int screen;
    xcb_visualtype_t *vistype;
    xcb_generic_event_t *event;
    xcb_window_t win;
    int curs_choice = CURS_NONE;
    char o;
    int optind = 0;
    struct option longopts[] = {
        {"version", no_argument, NULL, 'v'},
        {"nofork", no_argument, NULL, 'n'},
        {"beep", no_argument, NULL, 'b'},
        {"dpms", no_argument, NULL, 'd'},
        {"color", required_argument, NULL, 'c'},
        {"pointer", required_argument, NULL , 'p'},
#ifndef NOLIBCAIRO
        {"image", required_argument, NULL, 'i'},
        {"tiling", no_argument, NULL, 't'},
#endif
        {NULL, no_argument, NULL, 0}
    };

    if ((username = getenv("USER")) == NULL)
        errx(1, "USER environment variable not set, please set it.\n");

    while ((o = getopt_long(argc, argv, "vnbdc:p:"
#ifndef NOLIBCAIRO
        "i:t"
#endif
        , longopts, &optind)) != -1) {
        switch (o) {
        case 'v':
            errx(EXIT_SUCCESS, "version " VERSION " © 2010-2011 Michael Stapelberg\n");
        case 'n':
            dont_fork = true;
            break;
        case 'b':
            beep = true;
            break;
        case 'd':
            dpms = true;
            break;
        case 'c': {
            char *arg = optarg;

            /* Skip # if present */
            if (arg[0] == '#')
                arg++;

            if (strlen(arg) != 6 || sscanf(arg, "%06[0-9a-fA-F]", color) != 1)
                errx(1, "color is invalid, color must be given in 6-byte format: rrggbb\n");

            break;
        }
#ifndef NOLIBCAIRO
        case 'i':
            image_path = strdup(optarg);
            break;
        case 't':
            tile = true;
            break;
#endif
        case 'p':
            if (!strcmp(optarg, "win")) {
                curs_choice = CURS_WIN;
            } else if (!strcmp(optarg, "default")) {
                curs_choice = CURS_DEFAULT;
            } else {
                errx(1, "i3lock: Invalid pointer type given. Expected one of \"win\" or \"default\".\n");
            }
            break;
        default:
            errx(1, "i3lock: Unknown option. Syntax: i3lock [-v] [-n] [-b] [-d] [-c color] [-p win|default]"
#ifndef NOLIBCAIRO
            " [-i image.png] [-t]"
#else
            " (compiled with NOLIBCAIRO)"
#endif
            "\n");
        }
    }

    /* Initialize PAM */
    ret = pam_start("i3lock", username, &conv, &pam_handle);
    if (ret != PAM_SUCCESS)
        errx(EXIT_FAILURE, "PAM: %s\n", pam_strerror(pam_handle, ret));

    /* Initialize connection to X11 */
    if ((conn = xcb_connect(NULL, &screen)) == NULL ||
        xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Could not connect to X11, maybe you need to set DISPLAY?");

    if (!dont_fork) {
        /* In the parent process, we exit */
        if (fork() != 0)
            return 0;
    }

    /* if DPMS is enabled, check if the X server really supports it */
    if (dpms) {
        xcb_dpms_capable_cookie_t dpmsc = xcb_dpms_capable(conn);
        xcb_dpms_capable_reply_t *dpmsr;
        if ((dpmsr = xcb_dpms_capable_reply(conn, dpmsc, NULL)) && !dpmsr->capable) {
            fprintf(stderr, "Disabling DPMS, X server not DPMS capable\n");
            dpms = false;
        }
    }

    scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    vistype = get_root_visual_type(scr);

    uint32_t last_resolution[2] = {scr->width_in_pixels, scr->height_in_pixels};



#ifndef NOLIBCAIRO
    if (image_path) {
        /* Create a pixmap to render on, fill it with the background color */
        img = cairo_image_surface_create_from_png(image_path);
    }
#endif

    /* Pixmap on which the image is rendered to (if any) */
    xcb_pixmap_t bg_pixmap = draw_image(vistype, last_resolution, color);

    /* open the fullscreen window, already with the correct pixmap in place */
    win = open_fullscreen_window(conn, scr, color, bg_pixmap);

    cursor = create_cursor(conn, scr, win, curs_choice);

    grab_pointer_and_keyboard(conn, scr, cursor);

    symbols = xcb_key_symbols_alloc(conn);
    modeswitchmask = get_mod_mask(conn, symbols, XK_Mode_switch);
    numlockmask = get_mod_mask(conn, symbols, XK_Num_Lock);

    if (dpms)
        dpms_turn_off_screen(conn);

    while ((event = xcb_wait_for_event(conn))) {
        if (event->response_type == 0)
            errx(1, "XCB: Invalid event received");

        /* Strip off the highest bit (set if the event is generated) */
        int type = (event->response_type & 0x7F);

        if (type == XCB_KEY_PRESS) {
            handle_key_press((xcb_key_press_event_t*)event);
            continue;
        }

        if (type == XCB_KEY_RELEASE) {
            handle_key_release((xcb_key_release_event_t*)event);

            /* If this was the backspace or escape key we are back at an
             * empty input, so turn off the screen if DPMS is enabled */
            if (dpms && input_position == 0)
                dpms_turn_off_screen(conn);

            continue;
        }

        if (type == XCB_VISIBILITY_NOTIFY) {
            handle_visibility_notify((xcb_visibility_notify_event_t*)event);
            continue;
        }

        if (type == XCB_MAPPING_NOTIFY) {
            handle_mapping_notify((xcb_mapping_notify_event_t*)event);
            continue;
        }

        if (type == XCB_CONFIGURE_NOTIFY) {
            handle_screen_resize(vistype, win, last_resolution, color);
            continue;
        }

        printf("WARNING: unhandled event of type %d\n", type);
    }

    return 0;
}
