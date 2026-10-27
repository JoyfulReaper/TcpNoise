CC = cc
CFLAGS = -Wall -Wextra -Wpedantic -g -Iinclude

SQLITE_CFLAGS != pkg-config --cflags sqlite3
SQLITE_LIBS != pkg-config --libs sqlite3

NATS_CFLAGS != pkg-config --cflags libnats
NATS_LIBS != pkg-config --libs libnats

CFLAGS += ${SQLITE_CFLAGS} ${NATS_CFLAGS}
LDLIBS += ${SQLITE_LIBS} ${NATS_LIBS}

SRC = src/main.c src/banner.c src/logging.c src/network.c src/database.c src/messaging.c src/http.c
OBJ = ${SRC:.c=.o}

tcpnoise: ${OBJ}
	${CC} ${OBJ} -o tcpnoise ${LDLIBS}

clean:
	rm -f ${OBJ} tcpnoise

test:
	${CC} ${CFLAGS} src/http.c tests/http_test.c -o /tmp/tcpnoise-http-test
	/tmp/tcpnoise-http-test
	${CC} ${CFLAGS} src/network.c src/http.c tests/network_test.c -o /tmp/tcpnoise-network-test
	/tmp/tcpnoise-network-test

.PHONY: clean test
