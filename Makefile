CC=gcc
CFLAGS=-std=c99 -Wextra -Wall -Werror -pedantic
LDFLAGS=-lm -pthread

ifeq ($(DEBUG),yes)
	CFLAGS += -g
	LDFLAGS +=
else
	CFLAGS += -O3 -DNDEBUG
	LDFLAGS +=
endif

EXC=emulation

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)


all:
ifeq ($(DEBUG),yes)
	@echo "Generating in debug mode"
else
	@echo "Generating in release mode"
endif
	@$(MAKE) $(EXC)

$(EXC): $(OBJ)
	@$(CC) -o $@ $^ $(LDFLAGS)


%.o: %.c
	@$(CC) -o $@ -c $< $(CFLAGS)

clean:
	@rm -rf $(OBJ) $(EXC)

.PHONY: clean mrproper

mrproper: clean
	@rm -rf $(EXC)


util.o: util.h

protocole.o: protocole.h util.h canal.h
canal.o: canal.h protocole.h util.h
test.o: canal.h protocole.h util.h

