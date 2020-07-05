#ifndef _I3LOCK_EXT_H
#define _I3LOCK_EXT_H

struct i3lock_extapi {
    void (*redraw)(void);
};

struct i3lock_ext {
    void (*init)(const struct i3lock_extapi *api);
    void (*draw_bg)(cairo_t *cr);
};

#endif
