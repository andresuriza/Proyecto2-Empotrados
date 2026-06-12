// hw_test_list.c — Reproductor con PLAYLIST + control next/prev/pausa desde el NIOS.
// Lee TODOS los .wav de una carpeta (por defecto la actual) y los reproduce en orden,
// con reproduccion continua (auto-siguiente al terminar).
//
// Protocolo mailbox (coincide con software/hello_world_small.c):
//   sh[0]=CMD (ARM->NIOS): 0 idle, 1 play, 2 stop
//   sh[1]=HEAD (ARM->NIOS), sh[2]=TAIL (NIOS->ARM)
//   sh[3]=REQ (NIOS->ARM): 0 nada, 1 next, 2 prev, 3 stop  <-- los botones del NIOS escriben aqui
//   sh[4]=STATE (NIOS->ARM): 0 detenido, 1 reproduciendo, 2 pausa  <-- el ARM lo muestra en la VGA
//   La PAUSA la maneja el NIOS (deja de drenar -> backpressure frena al ARM). No requiere nada aqui.
//   El STOP (KEY3) lo pide el NIOS con REQ_STOP=3: el ARM rebobina la MISMA cancion al inicio;
//   el NIOS queda "detenido" (no drena) hasta que se presione play. -> reinicia la cancion actual.
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
#define REQ_STOP    3   // KEY3: rebobinar la MISMA cancion al inicio y quedar detenido

#define IDX_STATE   4   // NIOS -> ARM: estado de reproduccion (para mostrarlo en la VGA)
#define ST_STOPPED  0
#define ST_PLAYING  1
#define ST_PAUSED   2

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

// --- VGA Text Controller (mismo LW bridge, offset 0x10000 -> 0xFF210000) ---
// Celda = word en (fila*80+col): [15:12]=bg [11:8]=fg [7:0]=ASCII. Paleta 16 colores (0 negro..15 blanco).
#define VGA_OFFSET 0x10000
#define VGA_COLS   40            // el controlador renderiza 40x8 chars (esquina sup-izq, 320x128 px)
#define VGA_ROWS   8
#define C_BLUE      1
#define C_GREEN    10
#define C_YELLOW   14
#define C_WHITE    15

static char    playlist[MAX_TRACKS][MAX_PATH];
static int     n_tracks = 0;
static int16_t chunk[CHUNK_FRAMES * 2]; // L,R intercalados (lo usa SOLO el hilo lector)

// --- ring de RAM (SPSC: lector escribe head, alimentador escribe tail) ---
static uint32_t          ring[RING_FRAMES];   // frames ya empacados (L<<16 | R)
static volatile uint32_t ring_head = 0;       // donde escribe el lector
static volatile uint32_t ring_tail = 0;       // donde lee el alimentador
static volatile int      reader_stop = 0;     // el alimentador pide parar al lector
static volatile int      reader_done = 0;     // el lector terminó el archivo (EOF)

static volatile uint32_t *vga = 0;   // base del controlador VGA (se setea en main)

static void vga_putc(int row, int col, char c, int fg, int bg) {
    if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS) return;
    vga[row * VGA_COLS + col] = ((uint32_t)(bg & 0xF) << 12) |
                                ((uint32_t)(fg & 0xF) << 8)  | (uint8_t)c;
}
static void vga_print(int row, int col, const char *s, int fg, int bg) {
    for (int i = 0; s[i] && (col + i) < VGA_COLS; i++) vga_putc(row, col + i, s[i], fg, bg);
}
static void vga_clear(int bg) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++) vga_putc(r, c, ' ', C_WHITE, bg);
}
// Pinta "ahora suena": titulo/artista/album + duracion total + numero de cancion.
// Si falta un tag, usa el filename. dur_sec = duracion total de la cancion en segundos.
static void vga_show_track(const char *title, const char *artist, const char *album,
                           const char *fname, int num, int total, uint32_t dur_sec) {
    char line[VGA_COLS + 1];
    const char *base = strrchr(fname, '/');
    base = base ? base + 1 : fname;
    // Solo 40x8 chars (esquina sup-izq). Layout compacto: filas 0..7, cols 0..39.
    vga_clear(C_BLUE);
    vga_print(0, 0, "= Reproductor de Audio =", C_YELLOW, C_BLUE);
    snprintf(line, sizeof line, "Titulo : %s", title[0]  ? title  : base);
    vga_print(2, 0, line, C_WHITE, C_BLUE);
    snprintf(line, sizeof line, "Artista: %s", artist[0] ? artist : "Desconocido");
    vga_print(3, 0, line, C_WHITE, C_BLUE);
    snprintf(line, sizeof line, "Album  : %s", album[0]  ? album  : "Desconocido");
    vga_print(4, 0, line, C_WHITE, C_BLUE);
    snprintf(line, sizeof line, "Duracion: %02u:%02u", (dur_sec / 60) % 100, dur_sec % 60);
    vga_print(5, 0, line, C_WHITE, C_BLUE);
    snprintf(line, sizeof line, "Cancion %d/%d", num, total);
    vga_print(6, 0, line, C_GREEN, C_BLUE);
}

// Indicador de estado (requisito PDF): reproduciendo / pausa / detenido. Fila 7.
static uint32_t last_vga_state = 0xFFFFFFFF;
static void vga_show_state(uint32_t st) {
    const char *txt = (st == ST_PAUSED)  ? "Estado: PAUSA        "
                    : (st == ST_STOPPED) ? "Estado: DETENIDO     "
                    :                      "Estado: Reproduciendo";
    vga_print(7, 0, txt, C_YELLOW, C_BLUE);
}
// Lee el estado que publica el NIOS en sh[4] y redibuja SOLO si cambió (no toca el audio).
static void vga_poll_state(volatile uint32_t *sh) {
    uint32_t st = sh[IDX_STATE];
    if (st != last_vga_state) { last_vga_state = st; vga_show_state(st); }
}

typedef struct {
    FILE    *wav;
    uint16_t channels;
    uint32_t total_frames;
} reader_arg_t;

// Parsea fmt + data + metadata (LIST/INFO: INAM=titulo, IART=artista, IPRD=album).
// Recorre chunks por tamaño (robusto). Deja el archivo posicionado al inicio del PCM.
// title/artist/album deben venir inicializados a "" por el caller.
static int parse_wav(FILE *f, uint16_t *channels_out, uint32_t *sample_rate_out,
                     uint16_t *bits_out, uint32_t *data_size_out,
                     char *title, char *artist, char *album) {
    char tag[4];
    uint32_t csize, u32;
    uint16_t u16;

    if (fread(tag, 4, 1, f) != 1 || memcmp(tag, "RIFF", 4)) return -1;
    fread(&u32, 4, 1, f);
    if (fread(tag, 4, 1, f) != 1 || memcmp(tag, "WAVE", 4)) return -1;

    int have_fmt = 0;
    while (fread(tag, 4, 1, f) == 1 && fread(&csize, 4, 1, f) == 1) {
        long next = ftell(f) + csize + (csize & 1);   // siguiente chunk (padding a par)

        if (memcmp(tag, "fmt ", 4) == 0) {
            fread(&u16, 2, 1, f);                      // audio format (1=PCM)
            fread(channels_out, 2, 1, f);
            fread(sample_rate_out, 4, 1, f);
            fread(&u32, 4, 1, f);                      // byte rate
            fread(&u16, 2, 1, f);                      // block align
            fread(bits_out, 2, 1, f);
            have_fmt = 1;
        }
        else if (memcmp(tag, "data", 4) == 0) {
            *data_size_out = csize;
            return have_fmt ? 0 : -1;                  // posicionado al inicio del PCM
        }
        else if (memcmp(tag, "LIST", 4) == 0) {
            char lt[4];
            fread(lt, 4, 1, f);
            if (memcmp(lt, "INFO", 4) == 0) {
                while (ftell(f) + 8 <= next) {
                    char sid[4]; uint32_t ss;
                    fread(sid, 4, 1, f); fread(&ss, 4, 1, f);
                    uint32_t n = ss < 63 ? ss : 63;
                    char val[64];
                    fread(val, 1, n, f); val[n] = '\0';
                    if      (!memcmp(sid, "INAM", 4)) strncpy(title,  val, 63);
                    else if (!memcmp(sid, "IART", 4)) strncpy(artist, val, 63);
                    else if (!memcmp(sid, "IPRD", 4)) strncpy(album,  val, 63);
                    fseek(f, (ss - n) + (ss & 1), SEEK_CUR);   // resto del valor + padding
                }
            }
        }
        fseek(f, next, SEEK_SET);                      // saltar al siguiente chunk
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
//   REQ_STOP(3) -> el NIOS pidio stop (main rebobina la MISMA cancion; el NIOS queda detenido)
static int play_track(volatile uint32_t *sh, const char *fname, int num, int total) {
    FILE *wav = fopen(fname, "rb");
    if (!wav) { perror("fopen"); return REQ_NEXT; }   // si falla, saltar

    uint16_t channels, bits;
    uint32_t sample_rate, data_size;
    char title[64] = {0}, artist[64] = {0}, album[64] = {0};
    if (parse_wav(wav, &channels, &sample_rate, &bits, &data_size,
                  title, artist, album) < 0 || bits != 16) {
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

    // duracion total = frames / sample_rate  (el dato sale del header WAV ya parseado)
    uint32_t dur_sec = sample_rate ? (data_size / (channels * 2)) / sample_rate : 0;

    printf("Reproduciendo: %s (%u Hz, %u ch, %u s)\n", fname, sample_rate, channels, dur_sec);
    vga_show_track(title, artist, album, fname, num, total, dur_sec);   // metadata en la VGA
    last_vga_state = 0xFFFFFFFF;   // forzar redibujo del estado para este tema (vga_show_track borro la fila)
    vga_poll_state(sh);            // dibujar el estado actual del NIOS ya

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
        vga_poll_state(sh);     // refrescar indicador de estado (reproduciendo) si cambió
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
                vga_poll_state(sh);   // en pausa/stop el ARM gira aca -> refresca el estado en la VGA
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
    vga = (volatile uint32_t *)((char *)lw + VGA_OFFSET);   // VGA en el mismo mapeo (0xFF210000)

    sh[IDX_CMD] = CMD_IDLE;
    sh[IDX_REQ] = REQ_NONE;

    // Reproduccion continua: avanza solo al terminar, o salta con next/prev del NIOS
    int idx = 0;
    while (1) {
        int r = play_track(sh, playlist[idx], idx + 1, n_tracks);
        if      (r == REQ_PREV) idx = (idx - 1 + n_tracks) % n_tracks;
        else if (r == REQ_STOP) { /* misma cancion: rebobinar al inicio (el NIOS queda detenido) */ }
        else                    idx = (idx + 1) % n_tracks;   // REQ_NEXT o fin natural
    }

    // (no se alcanza — reproduccion continua. Ctrl+C para salir.)
    munmap((void *)lw, MAP_SIZE);
    close(fd);
    return 0;
}