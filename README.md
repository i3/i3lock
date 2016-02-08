i3lock-color
============

*making i3lock's typing indicator color scheme configurable*

I added the following command line options:
* `--insidevercolor=rrggbbaa` -- Inside of the circle while the password is being verified
* `--insidewrongcolor=rrggbbaa` -- Inside of the circle when a wrong password was entered
* `--insidecolor=rrggbbaa` -- Inside of the circle while typing/idle
* `--ringvercolor=rrggbbaa` -- Outer ring while the password is being
* `--ringwrongcolor=rrggbbaa` -- Outer ring when a wrong password was entered
* `--ringcolor=rrggbbaa` -- Outer ring while typing/idle
* `--linecolor=rrggbbaa` -- Line separating outer ring from inside of the circle and delimiting the highlight segments
* `--textcolor=rrggbbaa` -- Text ("verifying", "wrong!")
* `--keyhlcolor=rrggbbaa` -- Keypress highlight segments
* `--bshlcolor=rrggbbaa` -- Backspace highlight segments

All the colors have an alpha channel now. Please keep in mind that this was not intended when the program was originally written, so making thing transparent that weren't before can make it look strange.

### Examples:

i3lock --insidevercolor=0000a0bf --insidewrongcolor=ff8000bf --insidecolor=ffffffbf --ringvercolor=0020ffff --ringwrongcolor=4040ffff --ringcolor=404090ff --textcolor=ffffffff --linecolor=aaaaaaff --keyhlcolor=30ccccff --bshlcolor=ff8000ff

Refer to the original README file for general information and libraries you need.
