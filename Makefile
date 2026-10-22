############################################################ LICENSE
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
#
############################################################ IDENT(1)
#
# $Title: bosd - on-screen display engine $
# $Copyright: 2026 Devin Teske. All rights reserved. $
# $FrauBSD$
#
############################################################ OPTIONS
PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
LIBDIR?=	${PREFIX}/lib
INCLUDEDIR?=	${PREFIX}/include
SHAREDIR?=	${PREFIX}/share
DATADIR?=	${SHAREDIR}/bosd
DOCSDIR?=	${SHAREDIR}/doc/bosd
MANDIR?=	${SHAREDIR}/man/man1
MAN3DIR?=	${SHAREDIR}/man/man3
EXAMPLEDIR?=	${SHAREDIR}/examples/bosd
TESTDIR?=	${DATADIR}/tests
PKGCONFIGDIR?=	${LIBDIR}/pkgconfig
PYTHON?=	python3
INSTALL_TESTS?=	yes

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra
PKG_CONFIG?=	pkg-config
# Includes and libs come from pkg-config only (no hard-coded PREFIX paths).
PKGS=		x11 xrandr xrender xext xft fontconfig libpng
PKG_CFLAGS!=	${PKG_CONFIG} --cflags ${PKGS}
PKG_LIBS!=	${PKG_CONFIG} --libs ${PKGS}
CPPFLAGS+=	-Iinclude -DBOSD_ICONDIR='"${DATADIR}"' ${PKG_CFLAGS}
LDLIBS=		${PKG_LIBS}

SHLIB_MAJOR=	4
SHLIB=		libbosd.so.${SHLIB_MAJOR}
LIBSRCS=	src/libbosd.c src/ipc.c src/escape.c
LIBOBJS=	${LIBSRCS:.c=.o}

PROG=		bosd
PROGSRCS=	src/main.c src/daemon.c src/png.c src/x11.c \
		src/osd_chrome.c src/draw_utf8.c src/badge.c \
		src/bar_geom.c src/bar_paint.c src/bar.c \
		src/countdown.c src/stext_paint.c src/stext.c
PROGOBJS=	${PROGSRCS:.c=.o}
MANIN=		man/bosd.1.in
MAN=		man/bosd.1
MAN3=		man/bosd.3
PICOMCONF=	examples/picom.conf

all: ${SHLIB} libbosd.so ${PROG} bosd.pc ${MAN} ${PICOMCONF}

${SHLIB}: ${LIBOBJS}
	${CC} -shared -Wl,-soname,${SHLIB} -o ${SHLIB} ${LIBOBJS}

libbosd.so: ${SHLIB}
	ln -sf ${SHLIB} libbosd.so

${PROG}: ${PROGOBJS} ${SHLIB} libbosd.so
	${CC} ${LDFLAGS} -o ${PROG} ${PROGOBJS} -L. -lbosd \
		-Wl,-rpath,\$$ORIGIN:\$$ORIGIN/../lib:${LIBDIR} ${LDLIBS}

${LIBOBJS}: CFLAGS+= -fPIC

.c.o:
	${CC} ${CFLAGS} ${CPPFLAGS} -c $< -o $@

${LIBOBJS} ${PROGOBJS}: include/bosd.h src/priv.h

${MAN}: ${MANIN} Makefile
	sed -e 's|@PREFIX@|${PREFIX}|g' \
	    -e 's|@DATADIR@|${DATADIR}|g' \
	    -e 's|@DOCSDIR@|${DOCSDIR}|g' \
	    ${MANIN} > ${MAN}

${PICOMCONF}: ${PICOMCONF}.in Makefile
	sed -e 's|@PREFIX@|${PREFIX}|g' ${PICOMCONF}.in > ${PICOMCONF}

bosd.pc: Makefile
	@printf '%s\n' \
	    'prefix=${PREFIX}' \
	    'exec_prefix=$${prefix}' \
	    'libdir=$${exec_prefix}/lib' \
	    'includedir=$${prefix}/include' \
	    '' \
	    'Name: bosd' \
	    'Description: bosd on-screen display client library' \
	    'Version: ${SHLIB_MAJOR}.0' \
	    'Libs: -L$${libdir} -lbosd' \
	    'Cflags: -I$${includedir}' \
	    > bosd.pc

EXAMPLE_PNGS=	examples/bsd.png \
		examples/airplane-on.png examples/airplane-off.png \
		examples/audio-speakers.png examples/audio-headphones.png \
		examples/auto-rotate-on.png examples/auto-rotate-off.png \
		examples/lid-awake.png examples/lid-s0ix.png \
		examples/mirror-on.png examples/mirror-off.png \
		examples/mirror-none.png examples/mirror-extend.png \
		examples/mirror-extend-none.png \
		examples/tile-on.png examples/tile-float.png

install: all ${EXAMPLE_PNGS}
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${LIBDIR} \
		${DESTDIR}${INCLUDEDIR} ${DESTDIR}${MANDIR} \
		${DESTDIR}${MAN3DIR} ${DESTDIR}${DATADIR} \
		${DESTDIR}${EXAMPLEDIR} ${DESTDIR}${DOCSDIR} \
		${DESTDIR}${PKGCONFIGDIR}
	install -m 555 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 555 bin/bosd-start ${DESTDIR}${BINDIR}/bosd-start
	install -m 555 ${SHLIB} ${DESTDIR}${LIBDIR}/${SHLIB}
	ln -sf ${SHLIB} ${DESTDIR}${LIBDIR}/libbosd.so
	install -m 444 include/bosd.h ${DESTDIR}${INCLUDEDIR}/bosd.h
	gzip -cn ${MAN} > ${DESTDIR}${MANDIR}/bosd.1.gz
	gzip -cn man/bosd-start.1 > ${DESTDIR}${MANDIR}/bosd-start.1.gz
	gzip -cn ${MAN3} > ${DESTDIR}${MAN3DIR}/bosd.3.gz
	chmod 444 ${DESTDIR}${MANDIR}/bosd.1.gz \
		${DESTDIR}${MANDIR}/bosd-start.1.gz \
		${DESTDIR}${MAN3DIR}/bosd.3.gz
	install -m 444 bosd.pc ${DESTDIR}${PKGCONFIGDIR}/bosd.pc
	install -m 444 ${EXAMPLE_PNGS} ${DESTDIR}${DATADIR}/
	install -m 444 examples/bsd.py examples/airplane.py \
		examples/audio.py examples/auto-rotate.py examples/lid.py \
		examples/mirror.py examples/tile.py tools/glyph.py \
		${PICOMCONF} \
		${DESTDIR}${EXAMPLEDIR}/
	install -m 444 README.md CHANGELOG.md ${DESTDIR}${DOCSDIR}/
.if ${INSTALL_TESTS} == yes
	install -d ${DESTDIR}${TESTDIR}
	install -m 555 tests/run ${DESTDIR}${TESTDIR}/
	install -m 555 tests/*.sh ${DESTDIR}${TESTDIR}/
.endif

example: ${EXAMPLE_PNGS}

examples/bsd.png: examples/bsd.py tools/glyph.py
	${PYTHON} examples/bsd.py examples/bsd.png

examples/airplane-on.png examples/airplane-off.png: \
		examples/airplane.py tools/glyph.py
	${PYTHON} examples/airplane.py examples

examples/audio-speakers.png examples/audio-headphones.png: \
		examples/audio.py tools/glyph.py
	${PYTHON} examples/audio.py examples

examples/auto-rotate-on.png examples/auto-rotate-off.png: \
		examples/auto-rotate.py tools/glyph.py
	${PYTHON} examples/auto-rotate.py examples

examples/lid-awake.png examples/lid-s0ix.png: \
		examples/lid.py tools/glyph.py
	${PYTHON} examples/lid.py examples

examples/mirror-on.png examples/mirror-off.png examples/mirror-none.png \
		examples/mirror-extend.png examples/mirror-extend-none.png: \
		examples/mirror.py tools/glyph.py
	${PYTHON} examples/mirror.py examples

examples/tile-on.png examples/tile-float.png: \
		examples/tile.py tools/glyph.py
	${PYTHON} examples/tile.py examples

clean:
	rm -f ${PROG} ${PROGOBJS} ${LIBOBJS} ${SHLIB} libbosd.so bosd.pc \
		${MAN} ${PICOMCONF} ${EXAMPLE_PNGS}
	rm -rf tools/__pycache__ examples/__pycache__

#
# Visual walkthrough: prints EXPECT lines, then paints with bosd -D
# Requires DISPLAY (and a compositor to judge -A/-O)
#
test: all
	./tests/run

.PHONY: all install clean example test
