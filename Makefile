CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra
SRC=src/arm_invaders_sim.c
BIN=arm_invaders_sim

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: all
	./$(BIN)

clean:
	rm -f $(BIN)

