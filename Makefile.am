@CODE_COVERAGE_RULES@

echo-version:
	@echo "@I3LOCK_VERSION@"

bin_PROGRAMS = i3lock

dist_man1_MANS = i3lock.1

pamddir = $(sysconfdir)/pam.d
pamd_files = pam/i3lock
pamd_DATA = $(pamd_files)

AM_CPPFLAGS = \
	@AX_EXTEND_SRCDIR_CPPFLAGS@

i3lock_CFLAGS = \
	$(AM_CFLAGS) \
	$(XCB_CFLAGS) \
	$(XCB_IMAGE_CFLAGS) \
	$(XCB_UTIL_CFLAGS) \
	$(XCB_UTIL_XRM_CFLAGS) \
	$(XKBCOMMON_CFLAGS) \
	$(CAIRO_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

i3lock_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(CODE_COVERAGE_CPPFLAGS)

i3lock_LDADD = \
	$(XCB_LIBS) \
	$(XCB_IMAGE_LIBS) \
	$(XCB_UTIL_LIBS) \
	$(XCB_UTIL_XRM_LIBS) \
	$(XKBCOMMON_LIBS) \
	$(CAIRO_LIBS) \
	$(CODE_COVERAGE_LDFLAGS)

i3lock_SOURCES = \
	cursors.h \
	dpi.c \
	dpi.h \
	i3lock.c \
	i3lock.h \
	randr.c \
	randr.h \
	unlock_indicator.c \
	unlock_indicator.h \
	xcb.c \
	xcb.h

EXTRA_DIST = \
	$(pamd_files) \
	CHANGELOG \
	LICENSE \
	README.md
