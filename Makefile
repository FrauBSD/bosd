# bosd - on-screen display engine
#
PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
SHAREDIR?=	${PREFIX}/share
MANDIR?=	${SHAREDIR}/man/man1
ICONDIR?=	${SHAREDIR}/bosd

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra
CPPFLAGS+=	-I/usr/local/include -DBOSD_ICONDIR='"${ICONDIR}"'
LDFLAGS+=	-L/usr/local/lib
LDLIBS=		-lX11 -lXrandr -lXrender -lXext -lpng

PROG=		bosd
SRCS=		src/main.c src/daemon.c src/ipc.c src/png.c src/x11.c
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
		${DESTDIR}${ICONDIR}
	install -m 555 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	install -m 444 ${MAN} ${DESTDIR}${MANDIR}/bosd.1

clean:
	rm -f ${PROG} ${OBJS}

.PHONY: all install clean
