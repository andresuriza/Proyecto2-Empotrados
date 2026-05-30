# Proyecto2 MP3 preflight tests

These tests validate the control plane before the MP3 logic is enabled.

## Nios II test

File: `hello_world.c`

What it checks:

- JTAG UART output
- KEY and SW PIO reads
- HEX display writes
- Shared mailbox memory at `SHARED_MEM_BASE`

Mailbox layout:

- word 0: Nios signature `0x4E494F53` (`NIOS`)
- word 1: Nios heartbeat counter
- word 2: HPS signature `0x48505321` (`HPS!`)
- word 3: HPS heartbeat counter
- word 4: packed KEY/SW snapshot
- word 5: command word from HPS
- word 6: status word

Build and run from the Nios project directory:

```bash
make clean
make
make download-elf
```

If your environment uses the older command-line tools, `create-this-app` also documents the normal `nios2-download` flow.

## HPS Linux test

File: `hps_mailbox_test.c`

What it checks:

- HPS access to the FPGA lightweight bridge through `/dev/mem`
- HPS writes its own signature and heartbeat into the shared mailbox
- HPS reads back the Nios signature, counter, and KEY/SW snapshot

Compile and run on the HPS Linux shell:

```bash
gcc -O2 -Wall -o hps_mailbox_test hps_mailbox_test.c
sudo ./hps_mailbox_test
```

Notes:

- `hps_mailbox_test.c` assumes the standard Cyclone V lightweight bridge base `0xFF200000`.
- If your Platform Designer HPS bridge base was moved, update `LW_BRIDGE_BASE`.
- Once both tests pass, the shared mailbox can be reused for MP3 play/pause, filter selection, and status reporting.
