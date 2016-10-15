#!/bin/zsh
#scrot /tmp/screenshot.png
#i3lock
#convert /tmp/screenshot.png -blur 0x5 /tmp/screenshot.png
#convert /tmp/screenshot.png -scale 10% -scale 1000% /tmp/screenshot.png
#pkill i3lock && i3lock -i /tmp/screenshot.png

#IMG=/tmp/screenshot.png
IMG=/home/arcana/wall/arcwall2_1080.png

B='#00000000'  # blank
C='#ffffff22'  # clear ish
D='#ff00ffcc'  # default
T='#ee00eeee'  # text
W='#880000bb'  # wrong
V='#bb00bbbb'  # verifying

./i3lock              \
--image $IMG          \
--insidevercolor=$C   \
--ringvercolor=$V     \
\
--insidewrongcolor=$C \
--ringwrongcolor=$W   \
\
--insidecolor=$B      \
--ringcolor=$D        \
--linecolor=$B        \
--separatorcolor=$D   \
\
--textcolor=$T        \
--keyhlcolor=$W       \
--bshlcolor=$W        \
\
--screen 0            \
--clock               \
--timestr="%H:%M:%S"  \
--datestr="%A, %m %Y" 
