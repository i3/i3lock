# i3lock version
VERSION = 1.0

# Customize below to fit your system

# paths
PREFIX = /usr

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

MANDIR = $(DESTDIR)$(PREFIX)/share/man

# includes and libs
INCS = -I. -I/usr/include -I${X11INC}
LIBS = -L${X11LIB} -lX11 -lpam -lXext -lXpm -lm

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
INSTALL=install
