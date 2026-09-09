# bosd - on-screen display engine
#
PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
SHAREDIR?=	${PREFIX}/share
MANDIR?=	${SHAREDIR}/man/man1
ICONDIR?=	${SHAREDIR}/bosd
EXAMPLEDIR?=	${SHAREDIR}/examples/bosd

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra
PKG_CONFIG?=	pkg-config
# Includes and libs come from pkg-config only (no hard-coded PREFIX paths).
PKGS=		x11 xrandr xrender xext xft fontconfig libpng
PKG_CFLAGS!=	${PKG_CONFIG} --cflags ${PKGS}
PKG_LIBS!=	${PKG_CONFIG} --libs ${PKGS}
CPPFLAGS+=	-DBOSD_ICONDIR='"${ICONDIR}"' ${PKG_CFLAGS}
LDLIBS=		${PKG_LIBS}

PROG=		bosd
SRCS=		src/main.c src/daemon.c src/ipc.c src/png.c src/x11.c \
		src/badge.c src/bar.c src/countdown.c src/stext.c
OBJS=		${SRCS:.c=.o}
MAN=		man/bosd.1

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o ${PROG} ${OBJS} ${LDLIBS}

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

${OBJS}: src/bosd.h

install: all
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR} \
		${DESTDIR}${ICONDIR} ${DESTDIR}${EXAMPLEDIR}
	install -m 555 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 444 ${MAN} ${DESTDIR}${MANDIR}/bosd.1
	install -m 444 examples/bsd.py tools/glyph.py \
		${DESTDIR}${EXAMPLEDIR}/

example: examples/bsd.png

examples/bsd.png: examples/bsd.py tools/glyph.py
	python3 examples/bsd.py examples/bsd.png

clean:
	rm -f ${PROG} ${OBJS} examples/bsd.png
	rm -rf tools/__pycache__ examples/__pycache__

.PHONY: all install clean example
