CC_PC  = gcc
CC_ARM = arm-none-eabi-gcc

# ── PC (prueba storage/FAT) ──────────────────────────────
CFLAGS_PC = -Wall -g -Iinclude -Ilib

SRC_PC = src/storage_manager.c       \
         src/wav_parser.c            \
         src/bsp/sim/diskio_sim.c    \
         lib/fatfs/ff.c              \
         src/test_storage.c

# ── ARM QEMU ─────────────────────────────────────────────
CFLAGS_ARM = -mcpu=cortex-a9 -marm  \
             -DTARGET_SIMULATION     \
             -Wall -g                \
             -ffreestanding          \
             -Iinclude -Ilib

SRC_ARM = src/startup.S                   \
          src/main.c                      \
          src/player.c                    \
          src/bsp/sim/bsp_buttons_sim.c   \
          src/syscalls.c

.PHONY: pc arm run clean

pc:
	mkdir -p build
	$(CC_PC) $(CFLAGS_PC) $(SRC_PC) -o build/test_storage

arm:
	mkdir -p build
	$(CC_ARM) $(CFLAGS_ARM) $(SRC_ARM) \
	-T linker.ld                        \
	-o build/player_sim.elf             \
	-nostartfiles

run: arm
	qemu-system-arm          \
	  -machine vexpress-a9   \
	  -cpu cortex-a9         \
	  -m 128M                \
	  -nographic             \
	  -audio none            \
	  -kernel build/player_sim.elf

clean:
	rm -rf build/