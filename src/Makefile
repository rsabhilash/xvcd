O ?= .
OBJ ?= $(O)/obj
CFLAGS=-Werror -Wall -Os -g -lftdi

all: $(O) $(O)/xvcdbb $(O)/xvcdmp

.PHONY: $(O) $(OBJ)

$(O):
	mkdir -p $(O)

$(OBJ):
	mkdir -p $(OBJ)

$(O)/xvcdbb: $(OBJ)/io_ftdi.o $(OBJ)/xvcd.o
	$(CC) -o $@ $^ $(CFLAGS)

$(O)/xvcdmp: $(OBJ)/io_ftdi_mpsse.o $(OBJ)/xvcd.o
	$(CC) -o $@ $^ $(CFLAGS)

$(OBJ)/%.o: %.c $(OBJ) io_ftdi.h
	$(CC) -c $< -o $@ $(CFLAGS)
