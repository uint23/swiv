CC ?= cc
PKG_CONFIG ?= pkg-config

.if defined(.MAKE)
PKG_CFLAGS != ${PKG_CONFIG} --cflags wayland-client pixman-1
PKG_LIBS != ${PKG_CONFIG} --libs wayland-client pixman-1
.else
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags wayland-client pixman-1)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs wayland-client pixman-1)
.endif

CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic
CFLAGS += -I.. -I./protocol
CFLAGS += ${PKG_CFLAGS}

LDFLAGS ?=
LDFLAGS += -Wl,-rpath,'$$ORIGIN/..'

LDLIBS += -L.. -lwld
LDLIBS += ${PKG_LIBS}

SOURCES = \
	wiv.c \
	image.c \
	protocol/xdg-shell-protocol.c

OBJECTS = ${SOURCES:.c=.o}

.PHONY: all clean

all: wiv

protocol/xdg-shell-protocol.o: protocol/xdg-shell-protocol.c
	${CC} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

wiv: $(OBJECTS)
	${CC} ${LDFLAGS} -o $@ ${OBJECTS} ${LDLIBS}

clean:
	rm -f $(OBJECTS) wiv
