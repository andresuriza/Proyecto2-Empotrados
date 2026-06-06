// hw_test_list.c — Reproductor con PLAYLIST + control next/prev/pausa desde el NIOS.
// Lee TODOS los .wav de una carpeta (por defecto la actual) y los reproduce en orden,
// con reproduccion continua (auto-siguiente al terminar).
//
// Protocolo mailbox (coincide con software/hello_world_small.c):
//   sh[0]=CMD (ARM->NIOS): 0 idle, 1 play, 2 stop
//   sh[1]=HEAD (ARM->NIOS), sh[2]=TAIL (NIOS->ARM)
//   sh[3]=REQ (NIOS->ARM): 0 nada, 1 next, 2 prev  <-- los botones del NIOS escriben aqui
//   La PAUSA la maneja el NIOS (deja de drenar -> backpressure frena al ARM). No requiere nada aqui.
//
// Uso:  ./hw_test_list [carpeta]     (sin argumento usa la carpeta actual ".")

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    // strcasecmp
#include <dirent.h>     // opendir/readdir
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE  0xFF200000UL
#define MAP_SIZE        0x80000
#define SHARED_OFFSET   0x00000

// Mailbox
#define IDX_CMD     0
#define IDX_HEAD    1
#define IDX_TAIL    2
#define IDX_REQ     3   // NIOS -> ARM: peticion de control

#define CMD_IDLE    0
#define CMD_PLAY    1
#define CMD_STOP    2

#define REQ_NONE    0
#define REQ_NEXT    1
#define REQ_PREV    2

// Buffer circular
#define BUF_WORD_START  64
#define BUF_WORDS       16320

// Playlist
#define MAX_TRACKS  64
#define MAX_PATH    512

// Lectura por bloques: leer de a 2 bytes es muy lento y el ARM se atrasa de 48kHz
// (el buffer se vacia y el audio baja de tono hacia el final). Leemos chunks grandes.
#define CHUNK_FRAMES 2048               // debe ser < BUF_WORDS (16320)

static char    playlist[MAX_TRACKS][MAX_PATH];
static int     n_tracks = 0;
static int16_t chunk[CHUNK_FRAMES * 2]; // L,R intercalados (o mono)

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

    // buscar chunk data (deja el archivo posicionado al inicio del PCM)
    while (!feof(f)) {
        fread(tag, 4, 1, f);
        fread(data_size_out, 4, 1, f);
        if (memcmp(tag, "data", 4) == 0) return 0;
        fseek(f, *data_size_out, SEEK_CUR);
    }
    return -1;
}

static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Escanea la carpeta y llena playlist[] con los .wav (ordenados por nombre)
static int scan_wavs(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { perror("opendir"); return -1; }

    struct dirent *e;
    n_tracks = 0;
    while ((e = readdir(d)) != NULL && n_tracks < MAX_TRACKS) {
        const char *name = e->d_name;
        const char *dot  = strrchr(name, '.');
        if (dot && strcasecmp(dot, ".wav") == 0) {
            snprintf(playlist[n_tracks], MAX_PATH, "%s/%s", dir, name);
            n_tracks++;
        }
    }
    closedir(d);
    qsort(playlist, n_tracks, MAX_PATH, cmp_names);
    return n_tracks;
}

// Reproduce una pista. Devuelve:
//   REQ_NONE(0) -> termino sola (auto-siguiente)
//   REQ_NEXT(1) -> el NIOS pidio siguiente
//   REQ_PREV(2) -> el NIOS pidio anterior
static int play_track(volatile uint32_t *sh, const char *fname) {
    FILE *wav = fopen(fname, "rb");
    if (!wav) { perror("fopen"); return REQ_NEXT; }   // si falla, saltar

    uint16_t channels, bits;
    uint32_t sample_rate, data_size;
    if (parse_wav(wav, &channels, &sample_rate, &bits, &data_size) < 0 || bits != 16) {
        fprintf(stderr, "WAV no soportado (no 16-bit?): %s\n", fname);
        fclose(wav); return REQ_NEXT;
    }

    // --- preparar mailbox para la nueva cancion (resync con el NIOS) ---
    sh[IDX_CMD] = CMD_STOP;   // que el NIOS resetee tail y limpie el FIFO
    for (volatile int w = 0; w < 2000000 && sh[IDX_CMD] != CMD_IDLE; w++) {} // esperar ACK
    sh[IDX_HEAD] = 0;
    sh[IDX_TAIL] = 0;
    sh[IDX_REQ]  = REQ_NONE;
    uint32_t head = 0;
    sh[IDX_CMD]  = CMD_PLAY;

    printf("Reproduciendo: %s (%u Hz, %u ch)\n", fname, sample_rate, channels);

    uint32_t total_frames = data_size / (channels * 2);
    uint32_t frame_bytes  = channels * 2;
    uint32_t remaining    = total_frames;

    while (remaining > 0) {
        // leer un bloque grande de una sola vez (clave para no atrasarse de 48kHz)
        uint32_t want = (remaining < CHUNK_FRAMES) ? remaining : CHUNK_FRAMES;
        size_t   got  = fread(chunk, frame_bytes, want, wav);
        if (got == 0) break;   // EOF inesperado

        for (size_t j = 0; j < got; j++) {
            int16_t sl, sr;
            if (channels == 2) { sl = chunk[2 * j]; sr = chunk[2 * j + 1]; }
            else               { sl = chunk[j];     sr = sl; }   // mono -> duplicar

            uint32_t packed = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;

            // backpressure: esperar espacio, romper si hay peticion (incluye pausa via NIOS)
            uint32_t next = (head + 1) % BUF_WORDS;
            while (next == sh[IDX_TAIL]) {
                if (sh[IDX_REQ] != REQ_NONE) {
                    int r = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE;
                    fclose(wav); return r;
                }
            }

            sh[BUF_WORD_START + head] = packed;
            head = next;
        }

        sh[IDX_HEAD] = head;       // publicar la cabeza 1 vez por bloque (menos escrituras al bridge)
        remaining   -= got;

        // peticion de control entre bloques?
        if (sh[IDX_REQ] != REQ_NONE) {
            int r = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE;
            fclose(wav); return r;
        }
    }

    // fin natural: esperar a que el NIOS drene lo que queda (no cortar el final)
    while (sh[IDX_TAIL] != head) {
        if (sh[IDX_REQ] != REQ_NONE) {
            int r = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE;
            fclose(wav); return r;
        }
    }

    fclose(wav);
    return REQ_NONE;   // -> auto-siguiente
}

int main(int argc, char *argv[]) {
    const char *dir = (argc >= 2) ? argv[1] : ".";

    if (scan_wavs(dir) <= 0) {
        fprintf(stderr, "No se encontraron archivos .wav en '%s'\n", dir);
        return 1;
    }

    printf("Playlist: %d canciones en '%s'\n", n_tracks, dir);
    for (int i = 0; i < n_tracks; i++) printf("  [%d] %s\n", i, playlist[i]);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    volatile uint32_t *lw = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (lw == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    volatile uint32_t *sh = (volatile uint32_t *)((char *)lw + SHARED_OFFSET);

    sh[IDX_CMD] = CMD_IDLE;
    sh[IDX_REQ] = REQ_NONE;

    // Reproduccion continua: avanza solo al terminar, o salta con next/prev del NIOS
    int idx = 0;
    while (1) {
        int r = play_track(sh, playlist[idx]);
        if (r == REQ_PREV) idx = (idx - 1 + n_tracks) % n_tracks;
        else               idx = (idx + 1) % n_tracks;   // REQ_NEXT o fin natural
    }

    // (no se alcanza — reproduccion continua. Ctrl+C para salir.)
    munmap((void *)lw, MAP_SIZE);
    close(fd);
    return 0;
}