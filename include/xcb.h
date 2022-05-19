#ifndef _XCB_H
#define _XCB_H

#include <xcb/xcb.h>

extern xcb_connection_t *conn;
extern xcb_screen_t *screen;

xcb_visualtype_t *get_root_visual_type(xcb_screen_t *s);
xcb_pixmap_t create_bg_pixmap(xcb_connection_t *conn, xcb_screen_t *scr, u_int32_t *resolution, char *color);
xcb_window_t open_fullscreen_window(xcb_connection_t *conn, xcb_screen_t *scr, char *color, xcb_pixmap_t pixmap);
bool grab_pointer_and_keyboard(xcb_connection_t *conn, xcb_screen_t *screen, xcb_cursor_t cursor, int tries);
xcb_cursor_t create_cursor(xcb_connection_t *conn, xcb_screen_t *screen, xcb_window_t win, int choice);
xcb_window_t find_focused_window(xcb_connection_t *conn, const xcb_window_t root);
void set_focused_window(xcb_connection_t *conn, const xcb_window_t root, const xcb_window_t window);

#endif
