# slock - simple screen locker
# © 2006-2007 Anselm R. Garbe, Sander van Dijk
# © 2009 Michael Stapelberg

include config.mk

SRC = i3lock.c
OBJ = ${SRC:.c=.o}

all: options i3lock

options:
	@echo i3lock build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

i3lock: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f i3lock ${OBJ} i3lock-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p i3lock-${VERSION}
	@cp -R LICENSE Makefile README config.mk i3lock.1 ${SRC} i3lock-${VERSION}
	@tar -cf i3lock-${VERSION}.tar i3lock-${VERSION}
	@gzip i3lock-${VERSION}.tar
	@rm -rf i3lock-${VERSION}

install: all
	@echo installing executable file to $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(MANDIR)/man1
	$(INSTALL) -m 755 i3lock $(DESTDIR)$(PREFIX)/bin/i3lock
	$(INSTALL) -m 644 i3lock.1 $(MANDIR)/man1/i3lock.1

uninstall:
	@echo removing executable file from $(DESTDIR)$(PREFIX)/bin
	@rm -f $(DESTDIR)$(PREFIX)/bin/i3lock

.PHONY: all options clean dist install uninstall
