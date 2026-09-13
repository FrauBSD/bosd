# bosd - on-screen display engine
#
PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
LIBDIR?=	${PREFIX}/lib
INCLUDEDIR?=	${PREFIX}/include
SHAREDIR?=	${PREFIX}/share
DOCDIR?=	${SHAREDIR}/doc/bosd
MANDIR?=	${SHAREDIR}/man/man1
MAN3DIR?=	${SHAREDIR}/man/man3
ICONDIR?=	${SHAREDIR}/bosd
EXAMPLEDIR?=	${SHAREDIR}/examples/bosd
TESTDIR?=	${SHAREDIR}/bosd/tests
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
CPPFLAGS+=	-Iinclude -DBOSD_ICONDIR='"${ICONDIR}"' ${PKG_CFLAGS}
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

all: ${SHLIB} libbosd.so ${PROG} bosd.pc ${MAN}

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
	sed -e 's|@PREFIX@|${PREFIX}|g' ${MANIN} > ${MAN}

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

install: all example
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${LIBDIR} \
		${DESTDIR}${INCLUDEDIR} ${DESTDIR}${MANDIR} \
		${DESTDIR}${MAN3DIR} ${DESTDIR}${ICONDIR} \
		${DESTDIR}${EXAMPLEDIR} ${DESTDIR}${DOCDIR} \
		${DESTDIR}${PKGCONFIGDIR}
	install -m 555 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 555 ${SHLIB} ${DESTDIR}${LIBDIR}/${SHLIB}
	ln -sf ${SHLIB} ${DESTDIR}${LIBDIR}/libbosd.so
	install -m 444 include/bosd.h ${DESTDIR}${INCLUDEDIR}/bosd.h
	gzip -cn ${MAN} > ${DESTDIR}${MANDIR}/bosd.1.gz
	gzip -cn ${MAN3} > ${DESTDIR}${MAN3DIR}/bosd.3.gz
	chmod 444 ${DESTDIR}${MANDIR}/bosd.1.gz \
		${DESTDIR}${MAN3DIR}/bosd.3.gz
	install -m 444 bosd.pc ${DESTDIR}${PKGCONFIGDIR}/bosd.pc
	install -m 444 examples/bsd.png ${DESTDIR}${ICONDIR}/
	install -m 444 examples/bsd.py tools/glyph.py \
		${DESTDIR}${EXAMPLEDIR}/
	install -m 444 README.md ${DESTDIR}${DOCDIR}/
.if ${INSTALL_TESTS} == yes
	install -d ${DESTDIR}${TESTDIR}
	install -m 555 tests/run ${DESTDIR}${TESTDIR}/
	install -m 555 tests/*.sh ${DESTDIR}${TESTDIR}/
.endif

example: examples/bsd.png

examples/bsd.png: examples/bsd.py tools/glyph.py
	${PYTHON} examples/bsd.py examples/bsd.png

clean:
	rm -f ${PROG} ${PROGOBJS} ${LIBOBJS} ${SHLIB} libbosd.so bosd.pc \
		${MAN} examples/bsd.png
	rm -rf tools/__pycache__ examples/__pycache__

#
# Visual walkthrough: prints EXPECT lines, then paints with bosd -D
# Requires DISPLAY (and a compositor to judge -A/-O)
#
test: all
	./tests/run

.PHONY: all install clean example test
