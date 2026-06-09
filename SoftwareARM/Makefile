CC_PC  = gcc
CC_ARM = arm-none-eabi-gcc

CFLAGS_PC = -Wall -g -Iinclude -Ilib -DTARGET_SIMULATION

SRC_PC = src/storage_manager.c       \
         src/wav_parser.c            \
         src/circular_buffer.c       \
         src/mailbox.c               \
         src/player.c                \
         src/bsp/sim/diskio_sim.c    \
         lib/fatfs/ff.c              \
         src/main.c

CFLAGS_ARM = -mcpu=cortex-a9 -marm              \
             -DTARGET_HARDWARE                   \
             -Wall -O2                           \
             -ffreestanding                      \
             -mno-unaligned-access               \
             -fno-tree-loop-distribute-patterns  \
             -Iinclude -Ilib

SRC_ARM = src/startup.S              \
          src/storage_manager.c      \
          src/wav_parser.c           \
          src/mailbox.c              \
          src/player.c               \
          src/syscalls.c             \
          src/bsp/hw/diskio_hw.c     \
          lib/fatfs/ff.c             \
          src/main.c

.PHONY: pc arm run clean

pc:
	mkdir -p build
	$(CC_PC) $(CFLAGS_PC) $(SRC_PC) -o build/test_player

arm:
	mkdir -p build
	$(CC_ARM) $(CFLAGS_ARM) $(SRC_ARM) \
	-T linker.ld                        \
	-o build/player_hw.elf              \
	-nostartfiles

run: pc
	./build/test_player

clean:
	rm -rf build/