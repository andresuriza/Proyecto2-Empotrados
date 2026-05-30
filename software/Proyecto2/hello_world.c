#include "altera_avalon_pio_regs.h"
#include "io.h"
#include "sys/alt_stdio.h"
#include "system.h"

#define SHM_WORD0_NIOS_MAGIC 0x00
#define SHM_WORD1_NIOS_COUNT 0x04
#define SHM_WORD2_HPS_MAGIC 0x08
#define SHM_WORD3_HPS_COUNT 0x0C
#define SHM_WORD4_KEYSW 0x10
#define SHM_WORD5_COMMAND 0x14
#define SHM_WORD6_STATUS 0x18

#define NIOS_MAGIC 0x4E494F53u
#define HPS_MAGIC 0x48505321u

static const alt_u8 seg7_lut[16] = {
    0x40, 0x79, 0x24, 0x30,
    0x19, 0x12, 0x02, 0x78,
    0x00, 0x10, 0x08, 0x03,
    0x46, 0x21, 0x06, 0x0E};

static alt_u32 pack_hex4(alt_u8 d3, alt_u8 d2, alt_u8 d1, alt_u8 d0)
{
    return ((alt_u32)seg7_lut[d3 & 0xF] << 21) |
           ((alt_u32)seg7_lut[d2 & 0xF] << 14) |
           ((alt_u32)seg7_lut[d1 & 0xF] << 7) |
           (alt_u32)seg7_lut[d0 & 0xF];
}

static alt_u32 pack_hex2(alt_u8 d1, alt_u8 d0)
{
    return ((alt_u32)seg7_lut[d1 & 0xF] << 7) |
           (alt_u32)seg7_lut[d0 & 0xF];
}

static void delay(volatile alt_u32 cycles)
{
    while (cycles--)
    {
        /* busy wait */
    }
}

static void mailbox_write(alt_u32 offset, alt_u32 value)
{
    IOWR_32DIRECT(SHARED_MEM_BASE, offset, value);
}

static alt_u32 mailbox_read(alt_u32 offset)
{
    return IORD_32DIRECT(SHARED_MEM_BASE, offset);
}

int main(void)
{
    alt_u32 seconds = 0;
    alt_u32 last_keys = 0xFFFFFFFFu;
    alt_u32 last_sw = 0xFFFFFFFFu;
    alt_u32 last_hps_magic = 0xFFFFFFFFu;
    alt_u32 last_hps_count = 0xFFFFFFFFu;
    alt_u32 last_command = 0xFFFFFFFFu;

    alt_putstr("\nProyecto2 MP3 preflight test - Nios II\n");
    alt_putstr("- JTAG UART alive\n");
    alt_putstr("- KEY / SW / HEX PIOs enabled\n");
    alt_putstr("- Shared mailbox enabled\n");

    mailbox_write(SHM_WORD0_NIOS_MAGIC, NIOS_MAGIC);
    mailbox_write(SHM_WORD1_NIOS_COUNT, 0);
    mailbox_write(SHM_WORD2_HPS_MAGIC, 0);
    mailbox_write(SHM_WORD3_HPS_COUNT, 0);
    mailbox_write(SHM_WORD4_KEYSW, 0);
    mailbox_write(SHM_WORD5_COMMAND, 0);
    mailbox_write(SHM_WORD6_STATUS, 0x0000C0DEu);

    IOWR_ALTERA_AVALON_PIO_DATA(PIO_HEX_LO_BASE, pack_hex4(0, 0, 0, 0));
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_HEX_HI_BASE, pack_hex2(0, 0));

    while (1)
    {
        alt_u32 keys = IORD_ALTERA_AVALON_PIO_DATA(PIO_KEY_BASE) & 0xF;
        alt_u32 sw = IORD_ALTERA_AVALON_PIO_DATA(PIO_SW_BASE) & 0x7;
        alt_u32 minutes = (seconds / 60) % 100;
        alt_u32 secs = seconds % 60;
        alt_u32 hps_magic = mailbox_read(SHM_WORD2_HPS_MAGIC);
        alt_u32 hps_count = mailbox_read(SHM_WORD3_HPS_COUNT);
        alt_u32 command = mailbox_read(SHM_WORD5_COMMAND);

        mailbox_write(SHM_WORD1_NIOS_COUNT, seconds);
        mailbox_write(SHM_WORD4_KEYSW, (keys << 16) | (sw & 0x7));
        mailbox_write(SHM_WORD6_STATUS, (0xC0DEu << 16) | (seconds & 0xFFFFu));

        IOWR_ALTERA_AVALON_PIO_DATA(
            PIO_HEX_LO_BASE,
            pack_hex4(minutes / 10, minutes % 10, secs / 10, secs % 10));
        IOWR_ALTERA_AVALON_PIO_DATA(PIO_HEX_HI_BASE, pack_hex2(keys, sw));

        if (keys != last_keys || sw != last_sw ||
            hps_magic != last_hps_magic || hps_count != last_hps_count ||
            command != last_command)
        {
            alt_putstr("KEY=0x");
            alt_printf("%x", keys);
            alt_putstr(" SW=0x");
            alt_printf("%x", sw);
            alt_putstr(" time=");
            alt_printf("%d:%d", (int)minutes, (int)secs);
            alt_putstr(" HPS_MAGIC=0x");
            alt_printf("%x", hps_magic);
            alt_putstr(" HPS_COUNT=0x");
            alt_printf("%x", hps_count);
            alt_putstr(" CMD=0x");
            alt_printf("%x", command);
            alt_putstr("\n");

            last_keys = keys;
            last_sw = sw;
            last_hps_magic = hps_magic;
            last_hps_count = hps_count;
            last_command = command;
        }

        delay(3000000);
        seconds++;
    }

    return 0;
}
