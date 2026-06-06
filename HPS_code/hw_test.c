#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE  0xFF200000UL
#define MAP_SIZE        0x80000         // 512 KB
#define SHARED_OFFSET   0x00000         // shared_mem.s2 en offset 0 del LW bridge

// Mailbox — indices de palabras en shared_mem
#define IDX_CMD     0   // ARM escribe: 0=idle 1=play 2=stop
#define IDX_HEAD    1   // ARM escribe: indice de escritura del buffer
#define IDX_TAIL    2   // NIOS escribe: indice de lectura del buffer

// Buffer circular de audio
#define BUF_WORD_START  64              // byte 0x100 / 4 palabras
#define BUF_WORDS       16320           // palabras restantes en 64 KB

// 1 = escribir pocas muestras y despacio (test de flood/contencion). 0 = normal.
#define ARM_GENTLE_TEST 0

static int parse_wav(FILE *f, uint16_t *channels_out, uint32_t *sample_rate_out,
                     uint16_t *bits_out, uint32_t *data_size_out) {
    char tag[4];
    uint32_t u32;
    uint16_t u16;

    fread(tag, 4, 1, f);
    if (memcmp(tag, "RIFF", 4)) return -1;
    fread(&u32, 4, 1, f);
    fread(tag, 4, 1, f);
    if (memcmp(tag, "WAVE", 4)) return -1;

    // buscar chunk fmt
    while (!feof(f)) {
        fread(tag, 4, 1, f);
        fread(&u32, 4, 1, f);
        if (memcmp(tag, "fmt ", 4) == 0) {
            fread(&u16, 2, 1, f);                   // audio format (1=PCM)
            fread(channels_out, 2, 1, f);
            fread(sample_rate_out, 4, 1, f);
            fread(&u32, 4, 1, f);                   // byte rate
            fread(&u16, 2, 1, f);                   // block align
            fread(bits_out, 2, 1, f);
            break;
        }
        fseek(f, u32, SEEK_CUR);
    }

    // buscar chunk data
    while (!feof(f)) {
        fread(tag, 4, 1, f);
        fread(data_size_out, 4, 1, f);
        if (memcmp(tag, "data", 4) == 0) return 0;
        fseek(f, *data_size_out, SEEK_CUR);
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <archivo.wav>\n", argv[0]);
        return 1;
    }

    FILE *wav = fopen(argv[1], "rb");
    if (!wav) { perror("fopen"); return 1; }

    uint16_t channels, bits;
    uint32_t sample_rate, data_size;

    if (parse_wav(wav, &channels, &sample_rate, &bits, &data_size) < 0) {
        fprintf(stderr, "Error al parsear WAV\n");
        fclose(wav); return 1;
    }

    printf("WAV: %u Hz, %u canales, %u bits, %u bytes de data\n",
           sample_rate, channels, bits, data_size);

    if (bits != 16) {
        fprintf(stderr, "Solo se soportan WAV de 16 bits\n");
        fclose(wav); return 1;
    }

    // Mapear shared_mem via LW bridge
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); fclose(wav); return 1; }

    volatile uint32_t *lw = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (lw == MAP_FAILED) { perror("mmap"); fclose(wav); return 1; }

    volatile uint32_t *sh = (volatile uint32_t *)((char*)lw + SHARED_OFFSET);

    // --- LOOPBACK TEST: comprobar que la shared_mem realmente persiste ---
    sh[3] = 0xDEADBEEF;
    uint32_t rb3 = sh[3];
    printf("ARM loopback sh[3]: escribi 0xDEADBEEF, lei 0x%08X %s\n",
           rb3, (rb3 == 0xDEADBEEF) ? "OK" : "<-- FALLO, memoria NO persiste");

    // Inicializar mailbox
    sh[IDX_CMD]  = 0;
    sh[IDX_HEAD] = 0;
    sh[IDX_TAIL] = 0;   // limpiar TAIL residual de corrida anterior

    uint32_t rb_tail = sh[IDX_TAIL];
    printf("ARM sh[IDX_TAIL] readback tras escribir 0: 0x%08X %s\n",
           rb_tail, (rb_tail == 0) ? "OK" : "<-- FALLO");

    printf(">>> Pausa 5s: revisa la terminal del NIOS, debe mostrar sh[3]=0xDEADBEEF cmd=0\n");
    sleep(5);

    uint32_t head = 0;

    sh[IDX_CMD] = 1;    // PLAY
    printf("Reproduciendo...\n");

    uint32_t total_frames = data_size / (channels * 2); // frames de audio

#if ARM_GENTLE_TEST
    // TEST: escribir POCAS muestras y DESPACIO (sin inundar el bus).
    // Si el NIOS las drena sin crashear -> el crash es por flood/contencion.
    if (total_frames > 2000) total_frames = 2000;
    printf(">>> MODO GENTIL: %u frames con delay (sin flood)\n", total_frames);
#endif

    for (uint32_t i = 0; i < total_frames; i++) {
        int16_t sl = 0, sr = 0;
        fread(&sl, 2, 1, wav);
        if (channels == 2)
            fread(&sr, 2, 1, wav);
        else
            sr = sl;    // mono -> duplicar en ambos canales

        // empacar L y R en una palabra: bits[31:16]=L bits[15:0]=R
        uint32_t packed = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;

        // esperar si el buffer esta lleno
        uint32_t next = (head + 1) % BUF_WORDS;
        while (next == sh[IDX_TAIL]) {}

        sh[BUF_WORD_START + head] = packed;
        head = next;
        sh[IDX_HEAD] = head;

#if ARM_GENTLE_TEST
        usleep(500);   // ~2000 escrituras/seg: trafico minimo
#endif
    }

    sh[IDX_CMD] = 2;    // STOP
    printf("Listo.\n");

    munmap((void*)lw, MAP_SIZE);
    close(fd);
    fclose(wav);
    return 0;
}
