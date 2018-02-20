O ?= .
OBJ ?= $(O)/obj

BUILD_FLAGS=-Werror -Wall -Os

# Can change LATENCY_TIMER value by including it on the make command as: LATENCY_TIMER=12
ifdef LATENCY_TIMER
   LATENCY=-DLATENCY_TIMER=$(LATENCY_TIMER)
else
   LATENCY=
endif

ifdef USE_GETIFADDRS
   GETIFADDRS=-DUSE_GETIFADDRS=\"$(USE_GETIFADDRS)\"
else
   GETIFADDRS=-UUSE_GETIFADDRS
endif

# Want USE_ASYNC to be the default
ifdef USE_SYNC
   SYNC=-UUSE_ASYNC
else
   SYNC=-DUSE_ASYNC
endif

ifdef USE_LIBFTDI1
   CFLAGS=$(BUILD_FLAGS) $(GETIFADDRS) $(SYNC) $(LATENCY) -DUSE_LIBFTDI1
   LDFLAGS=$(BUILD_FLAGS) -lftdi1
else
   CFLAGS=$(BUILD_FLAGS) $(GETIFADDRS) $(SYNC) $(LATENCY) -UUSE_LIBFTDI1
   LDFLAGS=$(BUILD_FLAGS) -lftdi
endif

all: $(O) $(O)/xvcdbb $(O)/xvcdmp

.PHONY: $(O) $(OBJ)

$(O):
	mkdir -p $(O)

$(OBJ):
	mkdir -p $(OBJ)

$(O)/xvcdbb: $(OBJ)/io_ftdi.o $(OBJ)/xvcd.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(O)/xvcdmp: $(OBJ)/io_ftdi_mpsse.o $(OBJ)/xvcd.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ)/%.o: %.c $(OBJ) io_ftdi.h
	$(CC) -c $< -o $@ $(CFLAGS)
