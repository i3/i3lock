#!/bin/sh

B='#00000000'  # blank
C='#ffffff22'  # clear ish
D='#ff00ffcc'  # default
T='#ee00eeee'  # text
W='#880000bb'  # wrong
V='#bb00bbbb'  # verifying

./x86_64-pc-linux-gnu/i3lock \
--blur 5 \
--bar-indicator \
--bar-position h \
--bar-direction 1 \
--bar-max-height 50 \
--bar-base-width 50 \
--bar-color 000000cc \
--keyhlcolor 880088cc \
--bar-periodic-step 50 \
--bar-step 50 \
--bar-width 250 \
--redraw-thread \
\
--clock \
--force-clock \
--timepos 5:h-80 \
--timecolor 880088ff \
--datepos tx:ty+15 \
--datecolor 990099ff \
--date-align 1 \
--time-align 1 \
--ringvercolor 8800ff88 \
--ringwrongcolor ff008888 \
--statuspos 5:h-16 \
--verif-align 1 \
--wrong-align 1 \
--verifcolor ffffffff \
--wrongcolor ffffffff \
--modifpos -50:-50 \
\
--screen 1
