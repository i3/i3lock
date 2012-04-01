#ifndef _XINERAMA_H
#define _XINERAMA_H

typedef struct Rect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} Rect;

extern int xr_screens;
extern Rect *xr_resolutions;

void xinerama_init(void);
void xinerama_query_screens(void);

#endif
