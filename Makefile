CC      ?= cc
CFLAGS  ?= -O2 -ansi -pedantic -Wall -Wextra $(shell sdl2-config --cflags)
LDFLAGS ?= $(shell sdl2-config --libs)
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)
BIN     := gorph

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

HDR     := $(wildcard src/*.h)

src/%.o: src/%.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

wiperec: tools/wiperec.c src/armimg.h
	$(CC) $(CFLAGS) tools/wiperec.c -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN) wiperec

.PHONY: all clean
