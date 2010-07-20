#ifndef _XCB_H
#define _XCB_H

#include <xcb/xcb.h>

xcb_visualtype_t *get_root_visual_type(xcb_screen_t *s);
xcb_window_t open_fullscreen_window(xcb_connection_t *conn, xcb_screen_t *scr, char *color);
void grab_pointer_and_keyboard(xcb_connection_t *conn, xcb_screen_t *screen);
uint32_t get_mod_mask(xcb_connection_t *conn, xcb_key_symbols_t *symbols, uint32_t keycode);
void dpms_turn_off_screen(xcb_connection_t *conn);

#endif
