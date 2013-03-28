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
#include <err.h>
#include <assert.h>
#include <security/pam_appl.h>
#include <X11/Xlib-xcb.h>
#include <getopt.h>
#include <string.h>
#include <ev.h>
#include <sys/mman.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBfile.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "cursors.h"
#include "unlock_indicator.h"
#include "xinerama.h"

/* We need this for libxkbfile */
static Display *display;
char color[7] = "ffffff";
uint32_t last_resolution[2];
xcb_window_t win;
static xcb_cursor_t cursor;
static pam_handle_t *pam_handle;
int input_position = 0;
/* Holds the password you enter (in UTF-8). */
static char password[512];
static bool beep = false;
bool debug_mode = false;
static bool dpms = false;
bool unlock_indicator = true;
static bool dont_fork = false;
struct ev_loop *main_loop;
static struct ev_timer *clear_pam_wrong_timeout;
extern unlock_state_t unlock_state;
extern pam_state_t pam_state;

static struct xkb_state *xkb_state;
static struct xkb_context *xkb_context;
static struct xkb_keymap *xkb_keymap;

cairo_surface_t *img = NULL;
bool tile = false;

/* isutf, u8_dec © 2005 Jeff Bezanson, public domain */
#define isutf(c) (((c) & 0xC0) != 0x80)

/*
 * Decrements i to point to the previous unicode glyph
 *
 */
void u8_dec(char *s, int *i) {
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) || isutf(s[--(*i)]) || --(*i));
}

/*
 * Loads the XKB keymap from the X11 server and feeds it to xkbcommon.
 * Necessary so that we can properly let xkbcommon track the keyboard state and
 * translate keypresses to utf-8.
 *
 * Ideally, xkbcommon would ship something like this itself, but as of now
 * (version 0.2.0), it doesn’t.
 *
 */
static bool load_keymap(void) {
    bool ret = false;
    XkbFileInfo result;
    memset(&result, '\0', sizeof(result));
    result.xkb = XkbGetKeyboard(display, XkbAllMapComponentsMask, XkbUseCoreKbd);
    if (result.xkb == NULL) {
        fprintf(stderr, "[i3lock] XKB: XkbGetKeyboard failed\n");
        return false;
    }

    FILE *temp = tmpfile();
    if (temp == NULL) {
        fprintf(stderr, "[i3lock] could not create tempfile\n");
        return false;
    }

    bool ok = XkbWriteXKBKeymap(temp, &result, false, false, NULL, NULL);
    if (!ok) {
        fprintf(stderr, "[i3lock] XkbWriteXKBKeymap failed\n");
        goto out;
    }

    rewind(temp);

    if (xkb_context == NULL) {
        if ((xkb_context = xkb_context_new(0)) == NULL) {
            fprintf(stderr, "[i3lock] could not create xkbcommon context\n");
            goto out;
        }
    }

    if (xkb_keymap != NULL)
        xkb_keymap_unref(xkb_keymap);

    if ((xkb_keymap = xkb_keymap_new_from_file(xkb_context, temp, XKB_KEYMAP_FORMAT_TEXT_V1, 0)) == NULL) {
        fprintf(stderr, "[i3lock] xkb_keymap_new_from_file failed\n");
        goto out;
    }

    struct xkb_state *new_state = xkb_state_new(xkb_keymap);
    if (new_state == NULL) {
        fprintf(stderr, "[i3lock] xkb_state_new failed\n");
        goto out;
    }

    if (xkb_state != NULL)
        xkb_state_unref(xkb_state);
    xkb_state = new_state;

    ret = true;
out:
    XkbFreeKeyboard(result.xkb, XkbAllComponentsMask, true);
    fclose(temp);
    return ret;
}

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

static void clear_input(void) {
    input_position = 0;
    clear_password_memory();
    password[input_position] = '\0';

    /* Hide the unlock indicator after a bit if the password buffer is
     * empty. */
    start_clear_indicator_timeout();
    unlock_state = STATE_BACKSPACE_ACTIVE;
    redraw_screen();
    unlock_state = STATE_KEY_PRESSED;
}

static void input_done(void) {
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
    clear_input();
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
    xkb_state_update_key(xkb_state, event->detail, XKB_KEY_UP);
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
    xkb_keysym_t ksym;
    char buffer[128];
    int n;
    bool ctrl;

    ksym = xkb_state_key_get_one_sym(xkb_state, event->detail);
    ctrl = xkb_state_mod_name_is_active(xkb_state, "Control", XKB_STATE_MODS_DEPRESSED);
    xkb_state_update_key(xkb_state, event->detail, XKB_KEY_DOWN);

    /* The buffer will be null-terminated, so n >= 2 for 1 actual character. */
    memset(buffer, '\0', sizeof(buffer));
    n = xkb_keysym_to_utf8(ksym, buffer, sizeof(buffer));

    switch (ksym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_XF86ScreenSaver:
        password[input_position] = '\0';
        unlock_state = STATE_KEY_PRESSED;
        redraw_screen();
        input_done();
        return;

    case XKB_KEY_u:
        if (ctrl)
            clear_input();
        return;

    case XKB_KEY_Escape:
        clear_input();
        return;

    case XKB_KEY_BackSpace:
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

    if (n < 2)
        return;

    /* store it in the password array as UTF-8 */
    memcpy(password+input_position, buffer, n-1);
    input_position += n-1;
    DEBUG("current password = %.*s\n", input_position, password);

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
    /* We ignore errors — if the new keymap cannot be loaded it’s better if the
     * screen stays locked and the user intervenes by using killall i3lock. */
    (void)load_keymap();
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

    redraw_screen();

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
    if ((*resp = calloc(num_msg, sizeof(struct pam_response))) == NULL) {
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

                    ev_loop_fork(EV_DEFAULT);
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
    char *image_path = NULL;
    int ret;
    struct pam_conv conv = {conv_callback, NULL};
    int curs_choice = CURS_NONE;
    int o;
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
        {"image", required_argument, NULL, 'i'},
        {"tiling", no_argument, NULL, 't'},
        {NULL, no_argument, NULL, 0}
    };

    if ((username = getenv("USER")) == NULL)
        errx(1, "USER environment variable not set, please set it.\n");

    while ((o = getopt_long(argc, argv, "hvnbdc:p:ui:t", longopts, &optind)) != -1) {
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
                errx(1, "color is invalid, it must be given in 3-byte hexadecimal format: rrggbb\n");

            break;
        }
        case 'u':
            unlock_indicator = false;
            break;
        case 'i':
            image_path = strdup(optarg);
            break;
        case 't':
            tile = true;
            break;
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
            " [-i image.png] [-t]"
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

/* Using mlock() as non-super-user seems only possible in Linux. Users of other
 * operating systems should use encrypted swap/no swap (or remove the ifdef and
 * run i3lock as super-user). */
#if defined(__linux__)
    /* Lock the area where we store the password in memory, we don’t want it to
     * be swapped to disk. Since Linux 2.6.9, this does not require any
     * privileges, just enough bytes in the RLIMIT_MEMLOCK limit. */
    if (mlock(password, sizeof(password)) != 0)
        err(EXIT_FAILURE, "Could not lock page in memory, check RLIMIT_MEMLOCK");
#endif

    /* Initialize connection to X11 */
    if ((display = XOpenDisplay(NULL)) == NULL)
        errx(EXIT_FAILURE, "Could not connect to X11, maybe you need to set DISPLAY?");
    XSetEventQueueOwner(display, XCBOwnsEventQueue);
    conn = XGetXCBConnection(display);

    /* Double checking that connection is good and operatable with xcb */
    if (xcb_connection_has_error(conn))
        errx(EXIT_FAILURE, "Could not connect to X11, maybe you need to set DISPLAY?");

    /* When we cannot initially load the keymap, we better exit */
    if (!load_keymap())
        errx(EXIT_FAILURE, "Could not load keymap");

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

    if (image_path) {
        /* Create a pixmap to render on, fill it with the background color */
        img = cairo_image_surface_create_from_png(image_path);
        /* In case loading failed, we just pretend no -i was specified. */
        if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "Could not load image \"%s\": cairo surface status %d\n",
                    image_path, cairo_surface_status(img));
            img = NULL;
        }
    }

    /* Pixmap on which the image is rendered to (if any) */
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);

    /* open the fullscreen window, already with the correct pixmap in place */
    win = open_fullscreen_window(conn, screen, color, bg_pixmap);
    xcb_free_pixmap(conn, bg_pixmap);

    cursor = create_cursor(conn, screen, win, curs_choice);

    grab_pointer_and_keyboard(conn, screen, cursor);

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
