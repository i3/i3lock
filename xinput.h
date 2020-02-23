#ifndef _XINPUT_H
#define _XINPUT_H

#include <stdbool.h>
#include <xcb.h>

void xinput_init(void);
bool xinput_grab(xcb_window_t root_window);
void xinput_ungrab(void);

#endif
