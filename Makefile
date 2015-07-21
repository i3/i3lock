INSTALL=install
PREFIX=/usr
SYSCONFDIR=/etc
PKG_CONFIG=pkg-config

# Check if pkg-config is installed, we need it for building CFLAGS/LIBS
ifeq ($(shell which $(PKG_CONFIG) 2>/dev/null 1>/dev/null || echo 1),1)
$(error "$(PKG_CONFIG) was not found")
endif

CFLAGS += -std=c99
CFLAGS += -pipe
CFLAGS += -Wall
ifeq ($(DEBUG),1)
CFLAGS += -ggdb
endif
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += $(shell $(PKG_CONFIG) --cflags cairo xcb-dpms xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11)
LIBS += $(shell $(PKG_CONFIG) --libs cairo xcb-dpms xcb-xinerama xcb-atom xcb-image xcb-xkb xkbcommon xkbcommon-x11)
LIBS += -lpam
LIBS += -lev
LIBS += -lm
LIBS += -lutil

FILES:=$(wildcard *.c)
FILES:=$(FILES:.c=.o)

VERSION:=2.7
GIT_VERSION:="2.7 (2015-07-20) With Control Socket"
CPPFLAGS += -DVERSION=\"${GIT_VERSION}\"

.PHONY: install clean uninstall

all: i3lock

i3lock: ${FILES}
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f i3lock ${FILES} i3lock-${VERSION}.tar.gz *.service

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/pam.d
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
ifdef SERVICE
	 sed -e 's,%ARGS%,$(I3ARGS),g' \
		-e 's,%USERNAME%,$(USERNAME),g' \
		-e 's,%BIN%,$(DESTDIR)$(PREFIX)/bin/i3lock,g' \
		-e 's,%BIN_NAME%,i3lock,g' i3lock.service.in >i3lock.service
	$(INSTALL) -m 644 i3lock.service /usr/lib/systemd/system/i3lock.service
endif
	$(INSTALL) -m 644 i3lock.pam $(DESTDIR)$(SYSCONFDIR)/pam.d/i3lock

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock

dist: clean
	[ ! -d i3lock-${VERSION} ] || rm -rf i3lock-${VERSION}
	[ ! -e i3lock-${VERSION}.tar.bz2 ] || rm i3lock-${VERSION}.tar.bz2
	mkdir i3lock-${VERSION}
	cp *.c *.h i3lock.1 i3lock.pam Makefile LICENSE README.md CHANGELOG i3lock-${VERSION}
	sed -e 's/^GIT_VERSION:=\(.*\)/GIT_VERSION:=$(shell /bin/echo '${GIT_VERSION}' | sed 's/\\/\\\\/g')/g;s/^VERSION:=\(.*\)/VERSION:=${VERSION}/g' Makefile > i3lock-${VERSION}/Makefile
	tar cfj i3lock-${VERSION}.tar.bz2 i3lock-${VERSION}
	rm -rf i3lock-${VERSION}
