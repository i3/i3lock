TOPDIR=$(shell pwd)

INSTALL=install
PREFIX=/usr
SYSCONFDIR=/etc
PKG_CONFIG=pkg-config
MANDIR=/usr/share/man

# Check if pkg-config is installed, we need it for building CFLAGS/LIBS
ifeq ($(shell which $(PKG_CONFIG) 2>/dev/null 1>/dev/null || echo 1),1)
$(error "$(PKG_CONFIG) was not found")
endif

CFLAGS += -std=c99
CFLAGS += -pipe
CFLAGS += -Wall
CFLAGS += -O2
SIMD_CFLAGS += -funroll-loops
SIMD_CFLAGS += -msse2
CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -DXKBCOMPOSE=$(shell if test -e /usr/include/xkbcommon/xkbcommon-compose.h ; then echo 1 ; else echo 0 ; fi )
CFLAGS += $(shell $(PKG_CONFIG) --cflags cairo xcb-composite xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11)
LIBS += $(shell $(PKG_CONFIG) --libs cairo xcb-composite xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11)
LIBS += -lpam
LIBS += -lev
LIBS += -lm

# OpenBSD lacks PAM, use bsd_auth(3) instead.
ifneq ($(UNAME),OpenBSD)
  LIBS += -lpam
endif

FILES:=$(wildcard *.c)
FILES:=$(FILES:.c=.o)

ifeq ($(wildcard .git),)
  # not in git repository
  VERSION := $(shell [ -f $(TOPDIR)/I3LOCK_VERSION ] && cat $(TOPDIR)/I3LOCK_VERSION | cut -d '-' -f 1)
  I3LOCK_VERSION := '$(shell [ -f $(TOPDIR)/I3LOCK_VERSION ] && cat $(TOPDIR)/I3LOCK_VERSION)'
else
  VERSION:=$(shell git describe --tags --abbrev=0)
  I3LOCK_VERSION:="$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1))"
endif
CPPFLAGS += -DVERSION=\"${I3LOCK_VERSION}\"

.PHONY: install clean uninstall

all: i3lock

debug: CFLAGS += -g
debug: i3lock

blur_simd.o : CFLAGS += $(SIMD_CFLAGS)
i3lock: ${FILES}
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f i3lock ${FILES} i3lock-${VERSION}.tar.gz

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/pam.d
	$(INSTALL) -d $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
	$(INSTALL) -m 644 i3lock.pam $(DESTDIR)$(SYSCONFDIR)/pam.d/i3lock
	gzip -kf i3lock.1
	$(INSTALL) -m 644 i3lock.1.gz $(DESTDIR)$(MANDIR)/man1/i3lock.1.gz

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock
	rm -f $(DESTDIR)$(MANDIR)/man1/i3lock.1.gz

dist: clean
	[ ! -d i3lock-${VERSION} ] || rm -rf i3lock-${VERSION}
	[ ! -e i3lock-${VERSION}.tar.bz2 ] || rm i3lock-${VERSION}.tar.bz2
	mkdir i3lock-${VERSION}
	cp *.c *.h i3lock.1 i3lock.pam Makefile LICENSE README.md CHANGELOG i3lock-${VERSION}
	sed -e 's/^I3LOCK_VERSION:=\(.*\)/I3LOCK_VERSION:=$(shell /bin/echo '${I3LOCK_VERSION}' | sed 's/\\/\\\\/g')/g;s/^VERSION:=\(.*\)/VERSION:=${VERSION}/g' Makefile > i3lock-${VERSION}/Makefile
	tar cfj i3lock-${VERSION}.tar.bz2 i3lock-${VERSION}
	rm -rf i3lock-${VERSION}
