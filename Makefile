PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := PS-DiscordPresence.elf
SRCS := $(wildcard src/*.c)

CFLAGS := -Wall -O2 -Isrc

all: $(ELF)

$(ELF): $(SRCS) $(wildcard src/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

.PHONY: all clean test
