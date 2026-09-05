CC = cc
CFLAGS = -Wall -Wextra -Wpedantic -g -Iinclude

SQLITE_CFLAGS != pkg-config --cflags sqlite3
SQLITE_LIBS != pkg-config --libs sqlite3

CFLAGS += ${SQLITE_CFLAGS}
LDLIBS += ${SQLITE_LIBS}

SRC = src/main.c src/banner.c src/logging.c src/network.c src/database.c
OBJ = ${SRC:.c=.o}

tcpnoise: ${OBJ}
	${CC} ${OBJ} -o tcpnoise ${LDLIBS}

clean:
	rm -f ${OBJ} tcpnoise

.PHONY: clean
