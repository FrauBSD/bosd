# bosd - on-screen display engine
#
PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
LIBDIR?=	${PREFIX}/lib
INCLUDEDIR?=	${PREFIX}/include
SHAREDIR?=	${PREFIX}/share
MANDIR?=	${SHAREDIR}/man/man1
MAN3DIR?=	${SHAREDIR}/man/man3
ICONDIR?=	${SHAREDIR}/bosd
EXAMPLEDIR?=	${SHAREDIR}/examples/bosd
PKGCONFIGDIR?=	${LIBDIR}/pkgconfig

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
		src/badge.c src/bar.c src/countdown.c src/stext.c
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

install: all
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${LIBDIR} \
		${DESTDIR}${INCLUDEDIR} ${DESTDIR}${MANDIR} \
		${DESTDIR}${MAN3DIR} ${DESTDIR}${ICONDIR} \
		${DESTDIR}${EXAMPLEDIR} ${DESTDIR}${PKGCONFIGDIR}
	install -m 555 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 555 ${SHLIB} ${DESTDIR}${LIBDIR}/${SHLIB}
	ln -sf ${SHLIB} ${DESTDIR}${LIBDIR}/libbosd.so
	install -m 444 include/bosd.h ${DESTDIR}${INCLUDEDIR}/bosd.h
	install -m 444 ${MAN} ${DESTDIR}${MANDIR}/bosd.1
	install -m 444 ${MAN3} ${DESTDIR}${MAN3DIR}/bosd.3
	install -m 444 bosd.pc ${DESTDIR}${PKGCONFIGDIR}/bosd.pc
	install -m 444 examples/bsd.py tools/glyph.py \
		${DESTDIR}${EXAMPLEDIR}/

example: examples/bsd.png

examples/bsd.png: examples/bsd.py tools/glyph.py
	python3 examples/bsd.py examples/bsd.png

clean:
	rm -f ${PROG} ${PROGOBJS} ${LIBOBJS} ${SHLIB} libbosd.so bosd.pc \
		${MAN} examples/bsd.png
	rm -rf tools/__pycache__ examples/__pycache__

.PHONY: all install clean example
