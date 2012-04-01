INSTALL=install
PREFIX=/usr
SYSCONFDIR=/etc

# Check if pkg-config is installed, we need it for building CFLAGS/LIBS
ifeq ($(shell which pkg-config 2>/dev/null 1>/dev/null || echo 1),1)
$(error "pkg-config was not found")
endif

CFLAGS += -std=c99
CFLAGS += -pipe
CFLAGS += -Wall
CPPFLAGS += -D_GNU_SOURCE
ifndef NOLIBCAIRO
CFLAGS += $(shell pkg-config --cflags cairo xcb-keysyms xcb-dpms xcb-xinerama)
LIBS += $(shell pkg-config --libs cairo xcb-keysyms xcb-dpms xcb-xinerama xcb-image)
else
CPPFLAGS += -DNOLIBCAIRO
CFLAGS += $(shell pkg-config --cflags xcb-keysyms xcb-dpms xcb-xinerama)
LIBS += $(shell pkg-config --libs xcb-keysyms xcb-dpms xcb-image xcb-xinerama)
endif
LIBS += -lpam
LIBS += -lev
LIBS += -lX11

FILES:=$(wildcard *.c)
FILES:=$(FILES:.c=.o)

VERSION:=$(shell git describe --tags --abbrev=0)
GIT_VERSION:="$(shell git describe --tags --always) ($(shell git log --pretty=format:%cd --date=short -n1))"
CPPFLAGS += -DVERSION=\"${GIT_VERSION}\"

.PHONY: install clean uninstall

all: i3lock

i3lock: ${FILES}
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f i3lock ${FILES} i3lock-${VERSION}.tar.gz

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/pam.d
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
	$(INSTALL) -m 644 i3lock.pam $(DESTDIR)$(SYSCONFDIR)/pam.d/i3lock

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock

dist: clean
	[ ! -d i3lock-${VERSION} ] || rm -rf i3lock-${VERSION}
	[ ! -e i3lock-${VERSION}.tar.bz2 ] || rm i3lock-${VERSION}.tar.bz2
	mkdir i3lock-${VERSION}
	cp *.c *.h i3lock.1 i3lock.pam Makefile LICENSE README CHANGELOG i3lock-${VERSION}
	sed -e 's/^GIT_VERSION:=\(.*\)/GIT_VERSION:=$(shell /bin/echo '${GIT_VERSION}' | sed 's/\\/\\\\/g')/g;s/^VERSION:=\(.*\)/VERSION:=${VERSION}/g' Makefile > i3lock-${VERSION}/Makefile
	tar cfj i3lock-${VERSION}.tar.bz2 i3lock-${VERSION}
	rm -rf i3lock-${VERSION}
