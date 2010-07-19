INSTALL=install
PREFIX=/usr

# Check if pkg-config is installed, we need it for building CFLAGS/LDFLAGS
ifeq ($(shell which pkg-config 2>/dev/null 1>/dev/null || echo 1),1)
$(error "pkg-config was not found")
endif

CFLAGS += -std=c99
CFLAGS += -pipe
CFLAGS += -Wall
CFLAGS += -D_GNU_SOURCE
CFLAGS += $(shell pkg-config --cflags cairo xcb-keysyms xcb-dpms)
LDFLAGS += $(shell pkg-config --libs cairo xcb-keysyms xcb-dpms xcb-image)
LDFLAGS += -lpam

FILES:=$(wildcard *.c)
FILES:=$(FILES:.c=.o)

VERSION:=$(shell git describe --tags --abbrev=0)

.PHONY: install clean uninstall

all: i3lock

i3lock: ${FILES}
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f i3lock ${FILES} i3lock-${VERSION}.tar.gz

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/i3lock
