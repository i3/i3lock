/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010-2012 Michael Stapelberg
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
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <getopt.h>
#include <string.h>
#include <ev.h>
#include <sys/mman.h>


#ifndef NOLIBCAIRO
#include <cairo.h>
#include <cairo/cairo-xcb.h>
#endif

#include "i3lock.h"
#include "keysym2ucs.h"
#include "ucs2_to_utf8.h"
#include "xcb.h"
#include "cursors.h"
#include "unlock_indicator.h"
#include "xinerama.h"

char color[7] = "ffffff";
uint32_t last_resolution[2];
xcb_window_t win;
static xcb_cursor_t cursor;
static xcb_key_symbols_t *symbols;
static pam_handle_t *pam_handle;
int input_position = 0;
/* Holds the password you enter (in UTF-8). */
static char password[512];
static bool modeswitch_active = false;
static bool iso_level3_shift_active = false;
static bool iso_level5_shift_active = false;
static int numlockmask;
static int capslockmask;
static bool beep = false;
bool debug_mode = false;
static bool dpms = false;
bool unlock_indicator = true;
static bool dont_fork = false;
struct ev_loop *main_loop;
static struct ev_timer *clear_pam_wrong_timeout;
extern unlock_state_t unlock_state;
extern pam_state_t pam_state;

#ifndef NOLIBCAIRO
cairo_surface_t *img = NULL;
bool tile = false;
#endif

/*
 * Clears the memory which stored the password to be a bit safer against
 * cold-boot attacks.
 *
 */
static void clear_password_memory(void) {
    /* A volatile pointer to the password buffer to prevent the compiler from
     * optimizing this out. */
    volatile char *vpassword = password;
    for (int c = 0; c < sizeof(password); c++)
        /* We store a non-random pattern which consists of the (irrelevant)
         * index plus (!) the value of the beep variable. This prevents the
         * compiler from optimizing the calls away, since the value of 'beep'
         * is not known at compile-time. */
        vpassword[c] = c + (int)beep;
}


/*
 * Resets pam_state to STATE_PAM_IDLE 2 seconds after an unsuccesful
 * authentication event.
 *
 */
static void clear_pam_wrong(EV_P_ ev_timer *w, int revents) {
    DEBUG("clearing pam wrong\n");
    pam_state = STATE_PAM_IDLE;
    unlock_state = STATE_STARTED;
    redraw_screen();

    /* Now free this timeout. */
    ev_timer_stop(main_loop, clear_pam_wrong_timeout);
    free(clear_pam_wrong_timeout);
    clear_pam_wrong_timeout = NULL;
}

static void input_done(void) {
    if (input_position == 0)
        return;

    if (clear_pam_wrong_timeout) {
        ev_timer_stop(main_loop, clear_pam_wrong_timeout);
        free(clear_pam_wrong_timeout);
        clear_pam_wrong_timeout = NULL;
    }

    pam_state = STATE_PAM_VERIFY;
    redraw_screen();

    if (pam_authenticate(pam_handle, 0) == PAM_SUCCESS) {
        DEBUG("successfully authenticated\n");
        clear_password_memory();
        exit(0);
    }

    if (debug_mode)
        fprintf(stderr, "Authentication failure\n");

    pam_state = STATE_PAM_WRONG;
    redraw_screen();

    /* Clear this state after 2 seconds (unless the user enters another
     * password during that time). */
    ev_now_update(main_loop);
    if ((clear_pam_wrong_timeout = calloc(sizeof(struct ev_timer), 1))) {
        ev_timer_init(clear_pam_wrong_timeout, clear_pam_wrong, 2.0, 0.);
        ev_timer_start(main_loop, clear_pam_wrong_timeout);
    }

    /* Cancel the clear_indicator_timeout, it would hide the unlock indicator
     * too early. */
    stop_clear_indicator_timeout();

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
    DEBUG("releasing key %d, state raw = %d, modeswitch_active = %d, iso_level3_shift_active = %d, iso_level5_shift_active = %d\n",
          event->detail, event->state, modeswitch_active, iso_level3_shift_active, iso_level5_shift_active);

    /* We don’t care about the column here and just use the first symbol. Since
     * we only check for Mode_switch and ISO_Level3_Shift, this *should* work.
     * Also, if we would use the current column, we would look in the wrong
     * place. */
    xcb_keysym_t sym = xcb_key_press_lookup_keysym(symbols, event, 0);
    if (sym == XK_Mode_switch) {
        //printf("Mode switch disabled\n");
        modeswitch_active = false;
    } else if (sym == XK_ISO_Level3_Shift) {
        iso_level3_shift_active = false;
    } else if (sym == XK_ISO_Level5_Shift) {
        iso_level5_shift_active = false;
    }
    DEBUG("release done. modeswitch_active = %d, iso_level3_shift_active = %d, iso_level5_shift_active = %d\n",
          modeswitch_active, iso_level3_shift_active, iso_level5_shift_active);
}

static void redraw_timeout(EV_P_ ev_timer *w, int revents) {
    redraw_screen();

    ev_timer_stop(main_loop, w);
    free(w);
}

/*
 * Handle key presses. Fixes state, then looks up the key symbol for the
 * given keycode, then looks up the key symbol (as UCS-2), converts it to
 * UTF-8 and stores it in the password array.
 *
 */
static void handle_key_press(xcb_key_press_event_t *event) {
    DEBUG("keypress %d, state raw = %d, modeswitch_active = %d, iso_level3_shift_active = %d\n",
          event->detail, event->state, modeswitch_active, iso_level3_shift_active);

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
    if (iso_level5_shift_active)
        base_column = 6;
    sym0 = xcb_key_press_lookup_keysym(symbols, event, base_column);
    sym1 = xcb_key_press_lookup_keysym(symbols, event, base_column + 1);
    switch (sym0) {
    case XK_Mode_switch:
        DEBUG("Mode switch enabled\n");
        modeswitch_active = true;
        return;
    case XK_ISO_Level3_Shift:
        DEBUG("ISO_Level3_Shift enabled\n");
        iso_level3_shift_active = true;
        return;
    case XK_ISO_Level5_Shift:
        DEBUG("ISO_Level5_Shift enabled\n");
        iso_level5_shift_active = true;
        return;
    case XK_Return:
    case XK_KP_Enter:
        input_done();
    case XK_Escape:
        input_position = 0;
        clear_password_memory();
        password[input_position] = '\0';

        /* Hide the unlock indicator after a bit if the password buffer is
         * empty. */
        start_clear_indicator_timeout();
        unlock_state = STATE_BACKSPACE_ACTIVE;
        redraw_screen();
        unlock_state = STATE_KEY_PRESSED;
        return;

    case XK_BackSpace:
        if (input_position == 0)
            return;

        /* decrement input_position to point to the previous glyph */
        u8_dec(password, &input_position);
        password[input_position] = '\0';

        /* Hide the unlock indicator after a bit if the password buffer is
         * empty. */
        start_clear_indicator_timeout();
        unlock_state = STATE_BACKSPACE_ACTIVE;
        redraw_screen();
        unlock_state = STATE_KEY_PRESSED;
        return;
    }

    if ((input_position + 8) >= sizeof(password))
        return;

    /* Whether the user currently holds down the shift key. */
    bool shift = (event->state & XCB_MOD_MASK_SHIFT);

    /* Whether Caps Lock (all lowercase alphabetic keys will be replaced by
     * their uppercase variant) is active at the moment. */
    bool capslock = (event->state & capslockmask);

    DEBUG("shift = %d, capslock = %d\n",
          shift, capslock);

    if ((event->state & numlockmask) && xcb_is_keypad_key(sym1)) {
        /* this key was a keypad key */
        if (shift)
            sym = sym0;
        else sym = sym1;
    } else {
        xcb_keysym_t upper, lower;
        XConvertCase(sym0, (KeySym*)&lower, (KeySym*)&upper);
        DEBUG("sym0 = %c (%d), sym1 = %c (%d), lower = %c (%d), upper = %c (%d)\n",
              sym0, sym0, sym1, sym1, lower, lower, upper, upper);
        /* If there is no difference between the uppercase and lowercase
         * variant of this key, we consider Caps Lock off — it is only relevant
         * for alphabetic keys, unlike Shift Lock. */
        if (lower == upper) {
            capslock = false;
            DEBUG("lower == upper, now shift = %d, capslock = %d\n",
                  shift, capslock);
        }

        /* In two different cases we need to use the uppercase keysym:
         * 1) The user holds shift, no lock is active.
         * 2) Any of the two locks is active.
         */
        if ((shift && !capslock) || (!shift && capslock))
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

    DEBUG("resolved to keysym = %c (%d)\n", sym, sym);

    /* convert the keysym to UCS */
    uint16_t ucs = keysym2ucs(sym);
    if ((int16_t)ucs == -1) {
        if (debug_mode)
            fprintf(stderr, "Keysym could not be converted to UCS, skipping\n");
        return;
    }

    /* store the UCS in a string to convert it */
    uint8_t inp[3] = {(ucs & 0xFF00) >> 8, (ucs & 0xFF), 0};
    DEBUG("input part = %s\n", inp);

    /* store it in the password array as UTF-8 */
    input_position += convert_ucs_to_utf8((char*)inp, password + input_position);
    password[input_position] = '\0';
    DEBUG("current password = %s\n", password);

    unlock_state = STATE_KEY_ACTIVE;
    redraw_screen();
    unlock_state = STATE_KEY_PRESSED;

    struct ev_timer *timeout = calloc(sizeof(struct ev_timer), 1);
    if (timeout) {
        ev_timer_init(timeout, redraw_timeout, 0.25, 0.);
        ev_timer_start(main_loop, timeout);
    }

    stop_clear_indicator_timeout();
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

    numlockmask = get_mod_mask(conn, symbols, XK_Num_Lock);
}

/*
 * Called when the properties on the root window change, e.g. when the screen
 * resolution changes. If so we update the window to cover the whole screen
 * and also redraw the image, if any.
 *
 */
void handle_screen_resize(void) {
    xcb_get_geometry_cookie_t geomc;
    xcb_get_geometry_reply_t *geom;
    geomc = xcb_get_geometry(conn, screen->root);
    if ((geom = xcb_get_geometry_reply(conn, geomc, 0)) == NULL)
        return;

    if (last_resolution[0] == geom->width &&
        last_resolution[1] == geom->height) {
        free(geom);
        return;
    }

    last_resolution[0] = geom->width;
    last_resolution[1] = geom->height;

    free(geom);

#ifndef NOLIBCAIRO
    redraw_screen();
#endif

    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(conn, win, mask, last_resolution);
    xcb_flush(conn);

    xinerama_query_screens();
    redraw_screen();
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

/*
 * This callback is only a dummy, see xcb_prepare_cb and xcb_check_cb.
 * See also man libev(3): "ev_prepare" and "ev_check" - customise your event loop
 *
 */
static void xcb_got_event(EV_P_ struct ev_io *w, int revents) {
    /* empty, because xcb_prepare_cb and xcb_check_cb are used */
}

/*
 * Flush before blocking (and waiting for new events)
 *
 */
static void xcb_prepare_cb(EV_P_ ev_prepare *w, int revents) {
    xcb_flush(conn);
}

/*
 * Instead of polling the X connection socket we leave this to
 * xcb_poll_for_event() which knows better than we can ever know.
 *
 */
static void xcb_check_cb(EV_P_ ev_check *w, int revents) {
    xcb_generic_event_t *event;

    while ((event = xcb_poll_for_event(conn)) != NULL) {
        if (event->response_type == 0) {
            xcb_generic_error_t *error = (xcb_generic_error_t*)event;
            if (debug_mode)
                fprintf(stderr, "X11 Error received! sequence 0x%x, error_code = %d\n",
                        error->sequence, error->error_code);
            free(event);
            continue;
        }

        /* Strip off the highest bit (set if the event is generated) */
        int type = (event->response_type & 0x7F);
        switch (type) {
            case XCB_KEY_PRESS:
                handle_key_press((xcb_key_press_event_t*)event);
                break;

            case XCB_KEY_RELEASE:
                handle_key_release((xcb_key_release_event_t*)event);

                /* If this was the backspace or escape key we are back at an
                 * empty input, so turn off the screen if DPMS is enabled */
                if (dpms && input_position == 0)
                    dpms_turn_off_screen(conn);

                break;

            case XCB_VISIBILITY_NOTIFY:
                handle_visibility_notify((xcb_visibility_notify_event_t*)event);
                break;

            case XCB_MAP_NOTIFY:
                if (!dont_fork) {
                    /* After the first MapNotify, we never fork again. We don’t
                     * expect to get another MapNotify, but better be sure… */
                    dont_fork = true;

                    /* In the parent process, we exit */
                    if (fork() != 0)
                        exit(0);
                }
                break;

            case XCB_MAPPING_NOTIFY:
                handle_mapping_notify((xcb_mapping_notify_event_t*)event);
                break;

            case XCB_CONFIGURE_NOTIFY:
                handle_screen_resize();
                break;
        }

        free(event);
    }
}

int main(int argc, char *argv[]) {
    char *username;
#ifndef NOLIBCAIRO
    char *image_path = NULL;
#endif
    int ret;
    struct pam_conv conv = {conv_callback, NULL};
    int nscreen;
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
        {"debug", no_argument, NULL, 0},
        {"help", no_argument, NULL, 'h'},
        {"no-unlock-indicator", no_argument, NULL, 'u'},
#ifndef NOLIBCAIRO
        {"image", required_argument, NULL, 'i'},
        {"tiling", no_argument, NULL, 't'},
#endif
        {NULL, no_argument, NULL, 0}
    };

    if ((username = getenv("USER")) == NULL)
        errx(1, "USER environment variable not set, please set it.\n");

    while ((o = getopt_long(argc, argv, "hvnbdc:p:u"
#ifndef NOLIBCAIRO
        "i:t"
#endif
        , longopts, &optind)) != -1) {
        switch (o) {
        case 'v':
            errx(EXIT_SUCCESS, "version " VERSION " © 2010-2012 Michael Stapelberg");
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
        case 'u':
            unlock_indicator = false;
            break;
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
        case 0:
            if (strcmp(longopts[optind].name, "debug") == 0)
                debug_mode = true;
            break;
        default:
            errx(1, "Syntax: i3lock [-v] [-n] [-b] [-d] [-c color] [-u] [-p win|default]"
#ifndef NOLIBCAIRO
            " [-i image.png] [-t]"
#else
            " (compiled with NOLIBCAIRO)"
#endif
            );
        }
    }

    /* We need (relatively) random numbers for highlighting a random part of
     * the unlock indicator upon keypresses. */
    srand(time(NULL));

    /* Initialize PAM */
    ret = pam_start("i3lock", username, &conv, &pam_handle);
    if (ret != PAM_SUCCESS)
        errx(EXIT_FAILURE, "PAM: %s", pam_strerror(pam_handle, ret));

    /* Lock the area where we store the password in memory, we don’t want it to
     * be swapped to disk. Since Linux 2.6.9, this does not require any
     * privileges, just enough bytes in the RLIMIT_MEMLOCK limit. */
    if (mlock(password, sizeof(password)) != 0)
        err(EXIT_FAILURE, "Could not lock page in memory, check RLIMIT_MEMLOCK");

    /* Initialize connection to X11 */
    if ((conn = xcb_connect(NULL, &nscreen)) == NULL ||
        xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Could not connect to X11, maybe you need to set DISPLAY?");

    xinerama_init();
    xinerama_query_screens();

    /* if DPMS is enabled, check if the X server really supports it */
    if (dpms) {
        xcb_dpms_capable_cookie_t dpmsc = xcb_dpms_capable(conn);
        xcb_dpms_capable_reply_t *dpmsr;
        if ((dpmsr = xcb_dpms_capable_reply(conn, dpmsc, NULL))) {
            if (!dpmsr->capable) {
                if (debug_mode)
                    fprintf(stderr, "Disabling DPMS, X server not DPMS capable\n");
                dpms = false;
            }
            free(dpmsr);
        }
    }

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

    last_resolution[0] = screen->width_in_pixels;
    last_resolution[1] = screen->height_in_pixels;

    xcb_change_window_attributes(conn, screen->root, XCB_CW_EVENT_MASK,
            (uint32_t[]){ XCB_EVENT_MASK_STRUCTURE_NOTIFY });

#ifndef NOLIBCAIRO
    if (image_path) {
        /* Create a pixmap to render on, fill it with the background color */
        img = cairo_image_surface_create_from_png(image_path);
        /* In case loading failed, we just pretend no -i was specified. */
        if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
            if (debug_mode)
                fprintf(stderr, "Could not load image \"%s\": cairo surface status %d\n",
                        image_path, cairo_surface_status(img));
            img = NULL;
        }
    }
#endif

    /* Pixmap on which the image is rendered to (if any) */
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);

    /* open the fullscreen window, already with the correct pixmap in place */
    win = open_fullscreen_window(conn, screen, color, bg_pixmap);
    xcb_free_pixmap(conn, bg_pixmap);

    cursor = create_cursor(conn, screen, win, curs_choice);

    grab_pointer_and_keyboard(conn, screen, cursor);

    symbols = xcb_key_symbols_alloc(conn);
    numlockmask = get_mod_mask(conn, symbols, XK_Num_Lock);
    capslockmask = get_mod_mask(conn, symbols, XK_Caps_Lock);

    DEBUG("numlock mask = %d\n", numlockmask);
    DEBUG("caps lock mask = %d\n", capslockmask);

    if (dpms)
        dpms_turn_off_screen(conn);

    /* Initialize the libev event loop. */
    main_loop = EV_DEFAULT;
    if (main_loop == NULL)
        errx(EXIT_FAILURE, "Could not initialize libev. Bad LIBEV_FLAGS?\n");

    struct ev_io *xcb_watcher = calloc(sizeof(struct ev_io), 1);
    struct ev_check *xcb_check = calloc(sizeof(struct ev_check), 1);
    struct ev_prepare *xcb_prepare = calloc(sizeof(struct ev_prepare), 1);

    ev_io_init(xcb_watcher, xcb_got_event, xcb_get_file_descriptor(conn), EV_READ);
    ev_io_start(main_loop, xcb_watcher);

    ev_check_init(xcb_check, xcb_check_cb);
    ev_check_start(main_loop, xcb_check);

    ev_prepare_init(xcb_prepare, xcb_prepare_cb);
    ev_prepare_start(main_loop, xcb_prepare);

    /* Invoke the event callback once to catch all the events which were
     * received up until now. ev will only pick up new events (when the X11
     * file descriptor becomes readable). */
    ev_invoke(main_loop, xcb_check, 0);
    ev_loop(main_loop, 0);
}
