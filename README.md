i3lock - improved screen locker
===============================

_This is just a re-patched version of i3lock with the commits from [i3lock-color](https://github.com/eBrnd/i3lock-color); all the credit for the color functionality goes to [eBrnd](https://github.com/eBrnd/) !_

![i3lock-color in action. Why are you reading this?](https://github.com/PandorasFox/i3lock-color/raw/master/screenshot.png "Screenshot sample")

i3lock is a simple screen locker like slock. After starting it, you will
see a white screen (you can configure the color/an image). You can return
to your screen by entering your password.

Many little improvements have been made to i3lock over time:

- i3lock forks, so you can combine it with an alias to suspend to RAM
  (run "i3lock && echo mem > /sys/power/state" to get a locked screen
   after waking up your computer from suspend to RAM)

- You can specify either a background color or an image (JPG or PNG), which will be
  displayed while your screen is locked.

- You can specify whether i3lock should bell upon a wrong password.

- i3lock uses PAM and therefore is compatible with LDAP etc.
  On OpenBSD i3lock uses the bsd\_auth(3) framework.

## Additional features in this fork
  -  You can also specify additional options, as detailed in the manpage. This includes, but is not limited to, the following:
    - Color options for the following:
      - verification ring
      - interior ring color
      - ring interior line color
      - key highlight color
      - backspace highlight color
      - text colors for most/all strings
      - Changing all of the above depending on PAM's authentication status
    - Blurring the current screen and using that as the lock background    
    - Showing a clock in the indicator
    - refreshing on a timer, instead of on each keypress
    - Positioning the various UI elements
    - Changing the ring radius and thickness, as well as text size
    - A new bar indicator, which replaces the ring indicator with its own set of options
      - An experimental thread for driving the redraw ticks, so that things like the bar/clock still update when PAM is blocking

## Building

Before you build - check and see if there's a packaged version available for your distro (there usually is, either in a community repo/PPA).

If there's no packaged version available - think carefully, since you're using a forked screen locker at your own risk.

If you want to build a non-debug version, you should tag your build before configuring. For example: `git tag -f "git-$(git rev-parse --short HEAD)"` will add a tag with the short commit ID, which will be used for the version info.

i3lock now uses GNU autotools for building; you'll need to do something like `autoreconf -i && ./configure && make` to build.

### Required Packages
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libfontconfig-dev
- libxcb-composite0
- libxcb-composite0-dev
- libxcb-xinerama
- libxcb-randr
- libev
- libx11-xcb-dev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0
- libjpeg-turbo >= 1.4.90
#### Required Packages (Fedora 27)
- cairo-devel
- libev
- libev-devel
- libjpeg-devel
- libjpeg-turbo
- libxcb
- libxkbcommon
- libxkbcommon-x11
- libxkbcommon-x11-devel
- pam-devel
- pkg-config
- xcb-util-devel
- xcb-util-image
- xcb-util-image-devel

##### Aur Package
[Stable](https://aur.archlinux.org/packages/i3lock-color/)

[Git](https://aur.archlinux.org/packages/i3lock-color-git)

Running i3lock
-------------
Simply invoke the 'i3lock' command. To get out of it, enter your password and
press enter.

A [sample script](https://github.com/PandorasFox/i3lock-color/blob/master/lock.sh) is included in this repository. [Here](https://streamable.com/fpl46) is a short clip of that script in action!

On OpenBSD the `i3lock` binary needs to be setgid `auth` to call the
authentication helpers, e.g. `/usr/libexec/auth/login_passwd`.

Upstream
--------
Please submit pull requests for i3lock things to https://github.com/i3/i3lock and pull requests for features to me here at https://github.com/PandorasFox/i3lock-color.
