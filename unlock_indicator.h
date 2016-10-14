#ifndef _UNLOCK_INDICATOR_H
#define _UNLOCK_INDICATOR_H

typedef enum {
    STATE_STARTED = 0,         /* default state */
    STATE_KEY_PRESSED = 1,     /* key was pressed, show unlock indicator */
    STATE_KEY_ACTIVE = 2,      /* a key was pressed recently, highlight part
                                   of the unlock indicator. */
    STATE_BACKSPACE_ACTIVE = 3 /* backspace was pressed recently, highlight
                                   part of the unlock indicator in red. */
} unlock_state_t;

typedef enum {
    STATE_PAM_IDLE = 0,          /* no PAM interaction at the moment */
    STATE_PAM_VERIFY = 1,        /* currently verifying the password via PAM */
    STATE_PAM_LOCK = 2,          /* currently locking the screen */
    STATE_PAM_WRONG = 3,         /* the password was wrong */
    STATE_I3LOCK_LOCK_FAILED = 4 /* i3lock failed to load */
} pam_state_t;

xcb_pixmap_t draw_image(uint32_t* resolution);
void redraw_screen(void);
void clear_indicator(void);

#endif
