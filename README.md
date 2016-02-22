*** 
NOTE: a simple i3lock fork that allows you to execute whatever you want in case of wrong password. 
***

_Examples_:     

**take a photo from webcam on each incorrect password input:**

```bash
i3lock -E ./wrong.sh
```

_wrong.sh_

```bash
#!/usr/bin/env bash
ffmpeg -f video4linux2 -i /dev/video0 -s 1280x720 -vframes 1 ~/auth`date +%Y_%m_%d_%H-%M-%S`.jpeg &>/dev/null
```
**send email about wrong password**
```bash
i3lock -E "bash -c 'echo \"Someone typed wrong password\" | mail -s ok cve@example.org'"
```
_How to build:_

```
git clone https://github.com/loadaverage/i3lock.git
git checkout wrong_pass_exec
make && make install
```
Patches are located in ./patches diretory   
Apply patch to original and build:

```
patch < wrong_pass_exec_v{$version}.patch
make && make install
```

i3lock - improved screen locker
===============================
i3lock is a simple screen locker like slock. After starting it, you will
see a white screen (you can configure the color/an image). You can return
to your screen by entering your password.

Many little improvements have been made to i3lock over time:

- i3lock forks, so you can combine it with an alias to suspend to RAM
  (run "i3lock && echo mem > /sys/power/state" to get a locked screen
   after waking up your computer from suspend to RAM)

- You can specify either a background color or a PNG image which will be
  displayed while your screen is locked.

- You can specify whether i3lock should bell upon a wrong password.

- i3lock uses PAM and therefore is compatible with LDAP etc.

Requirements
------------
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libxcb-xinerama
- libev
- libx11-dev
- libx11-xcb-dev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0

Running i3lock
-------------
Simply invoke the 'i3lock' command. To get out of it, enter your password and
press enter.

Upstream
--------
Please submit pull requests to https://github.com/i3/i3lock
