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
// AUDIO SIN CORTES (doble-buffer): un HILO LECTOR lee la SD a un ring grande en RAM, y el
// bucle ALIMENTADOR saca del ring y escribe a la shared_mem (gateado por el NIOS = pitch 48kHz).
// Asi la latencia/ráfagas de la SD NO frenan el feed -> no hay underrun a los pocos segundos.
//
// Compilar (necesita pthreads):
//   arm-linux-gnueabihf-gcc -O2 -static -pthread -o hw_test_list HPS_code/hw_test_list.c
//   (o nativo en la placa:  gcc -O2 -pthread -o hw_test_list hw_test_list.c)
//
// Uso:  ./hw_test_list [carpeta]     (sin argumento usa la carpeta actual ".")

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    // strcasecmp
#include <dirent.h>     // opendir/readdir
#include <pthread.h>    // hilo lector
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

// Buffer circular en shared_mem (lo drena el NIOS)
#define BUF_WORD_START  64
#define BUF_WORDS       16320

// Playlist
#define MAX_TRACKS  64
#define MAX_PATH    512

// Tamaño del bloque que el lector pide a la SD de una vez.
#define CHUNK_FRAMES 2048               // debe ser < BUF_WORDS (16320)

// Ring de RAM entre el lector (SD) y el alimentador (shared_mem). Grande para absorber
// la lentitud/ráfagas de la SD: 256K frames = ~5.5 s a 48kHz = 1 MB en DDR3 (cacheado, rápido).
#define RING_FRAMES (256 * 1024)

static char    playlist[MAX_TRACKS][MAX_PATH];
static int     n_tracks = 0;
static int16_t chunk[CHUNK_FRAMES * 2]; // L,R intercalados (lo usa SOLO el hilo lector)

// --- ring de RAM (SPSC: lector escribe head, alimentador escribe tail) ---
static uint32_t          ring[RING_FRAMES];   // frames ya empacados (L<<16 | R)
static volatile uint32_t ring_head = 0;       // donde escribe el lector
static volatile uint32_t ring_tail = 0;       // donde lee el alimentador
static volatile int      reader_stop = 0;     // el alimentador pide parar al lector
static volatile int      reader_done = 0;     // el lector terminó el archivo (EOF)

typedef struct {
    FILE    *wav;
    uint16_t channels;
    uint32_t total_frames;
} reader_arg_t;

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

// HILO LECTOR: SD -> ring de RAM. No para nunca de leer (salvo que el ring se llene o
// se le pida parar). Aisla al alimentador de la latencia de la SD.
static void *reader_thread(void *arg) {
    reader_arg_t *ra = (reader_arg_t *)arg;
    uint32_t remaining   = ra->total_frames;
    uint32_t frame_bytes = ra->channels * 2;

    while (remaining > 0 && !reader_stop) {
        uint32_t want = (remaining < CHUNK_FRAMES) ? remaining : CHUNK_FRAMES;

        // esperar a que haya lugar en el ring para 'want' frames
        while (!reader_stop) {
            uint32_t used = (ring_head - ring_tail + RING_FRAMES) % RING_FRAMES;
            if ((RING_FRAMES - 1 - used) >= want) break;
            usleep(1000);
        }
        if (reader_stop) break;

        size_t got = fread(chunk, frame_bytes, want, ra->wav);
        if (got == 0) break;   // EOF

        uint32_t h = ring_head;
        for (size_t j = 0; j < got; j++) {
            int16_t sl, sr;
            if (ra->channels == 2) { sl = chunk[2 * j]; sr = chunk[2 * j + 1]; }
            else                   { sl = chunk[j];     sr = sl; }   // mono -> duplicar
            ring[h] = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;
            h = (h + 1) % RING_FRAMES;
        }
        __sync_synchronize();   // los datos del ring deben verse ANTES de publicar head
        ring_head = h;
        remaining -= got;
    }
    reader_done = 1;
    return NULL;
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

    // --- arrancar el hilo lector (SD -> ring de RAM) ---
    reader_stop = 0;
    reader_done = 0;
    ring_head = 0;
    ring_tail = 0;
    reader_arg_t ra = { wav, channels, data_size / (channels * 2) };
    pthread_t tid;
    pthread_create(&tid, NULL, reader_thread, &ra);

    int      ret = REQ_NONE;
    uint32_t pub = 0;        // cada cuanto publicar HEAD al bridge

    // --- bucle ALIMENTADOR (ring de RAM -> shared_mem). Igual que antes pero
    //     sacando del ring en vez de leer la SD aca. Pitch = NIOS = 48kHz. ---
    for (;;) {
        uint32_t rh = ring_head;
        __sync_synchronize();   // ver los datos que el lector escribió antes de publicar head

        if (ring_tail == rh) {              // ring vacío en este instante
            if (reader_done) break;         // se acabó la canción -> fin natural
            if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; goto stop; }
            continue;                       // el lector se está poniendo al día
        }

        while (ring_tail != rh) {
            uint32_t packed = ring[ring_tail];

            // backpressure: esperar espacio, romper si hay peticion (incluye pausa via NIOS)
            uint32_t next = (head + 1) % BUF_WORDS;
            while (next == sh[IDX_TAIL]) {
                if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; goto stop; }
            }

            sh[BUF_WORD_START + head] = packed;
            head = next;
            ring_tail = (ring_tail + 1) % RING_FRAMES;

            // publicar HEAD seguido (512 < BUF_WORDS) para que el NIOS siga drenando
            if (++pub >= 512) { sh[IDX_HEAD] = head; pub = 0; }
        }
        sh[IDX_HEAD] = head;   // publicar la cabeza al terminar el lote disponible
    }

    // fin natural: esperar a que el NIOS drene lo que queda (no cortar el final)
    sh[IDX_HEAD] = head;
    while (sh[IDX_TAIL] != head) {
        if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; break; }
    }

stop:
    reader_stop = 1;
    pthread_join(tid, NULL);   // espera a que el lector salga (a lo sumo un fread)
    fclose(wav);
    return ret;
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