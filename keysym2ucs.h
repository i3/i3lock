#ifndef _KEYSYM2UCS_H
#define _KEYSYM2UCS_H

#include <xcb/xcb.h>

long keysym2ucs(xcb_keysym_t keysym);

#endif
