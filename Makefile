CC = cc
CFLAGS = -Wall -Wextra -Wpedantic -g -Iinclude

SRC = src/main.c src/banner.c src/seen.c
OBJ = $(SRC:.c=.o)

tcpnoise: $(OBJ)
	$(CC) $(OBJ) -o tcpnoise

clean:
	rm -f $(OBJ) tcpnoise

.PHONY: clean
