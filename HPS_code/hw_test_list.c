#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE  0xFF200000UL
#define MAP_SIZE        0x80000
#define SHARED_OFFSET   0x00000

#define IDX_CMD     0
#define IDX_HEAD    1
#define IDX_TAIL    2
#define IDX_REQ     3

#define CMD_IDLE    0
#define CMD_PLAY    1
#define CMD_STOP    2

#define REQ_NONE    0
#define REQ_NEXT    1
#define REQ_PREV    2
#define REQ_STOP    3

#define IDX_STATE   4
#define ST_STOPPED  0
#define ST_PLAYING  1
#define ST_PAUSED   2

#define BUF_WORD_START  64
#define BUF_WORDS       16320

#define MAX_TRACKS  64
#define MAX_PATH    512

#define CHUNK_FRAMES 2048

#define RING_FRAMES (256 * 1024)

#define VGA_OFFSET 0x10000
#define VGA_COLS   40
#define VGA_ROWS   8
#define C_BLUE      1
#define C_GREEN    10
#define C_YELLOW   14
#define C_WHITE    15

static char    playlist[MAX_TRACKS][MAX_PATH];
static int     n_tracks = 0;
static int16_t chunk[CHUNK_FRAMES * 2];

static uint32_t          ring[RING_FRAMES];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;
static volatile int      reader_stop = 0;
static volatile int      reader_done = 0;

static volatile uint32_t *vga = 0;

// Escribe un carácter en una posición específica de la pantalla VGA.
static void vga_putc(int row, int col, char c, int fg, int bg) {
    if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS) return;
    vga[row * VGA_COLS + col] = ((uint32_t)(bg & 0xF) << 12) |
                                ((uint32_t)(fg & 0xF) << 8)  | (uint8_t)c;
}

// Escribe una cadena de texto en la pantalla VGA
static void vga_print(int row, int col, const char *s, int fg, int bg) {
    for (int i = 0; s[i] && (col + i) < VGA_COLS; i++) vga_putc(row, col + i, s[i], fg, bg);
}

// Limpia la pantalla VGA rellenando con espacios
static void vga_clear(int bg) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++) vga_putc(r, c, ' ', C_WHITE, bg);
}

// Muestra la información de la canción en reproducción en la VGA
static void vga_show_track(const char *title, const char *artist, const char *album,
                           const char *fname, int num, int total, uint32_t dur_sec) {
    char line[VGA_COLS + 1];
    const char *base = strrchr(fname, '/');
    base = base ? base + 1 : fname;
    
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

static uint32_t last_vga_state = 0xFFFFFFFF;

// Muestra el estado actual de reproducción en la VGA
static void vga_show_state(uint32_t st) {
    const char *txt = (st == ST_PAUSED)  ? "Estado: PAUSA        "
                    : (st == ST_STOPPED) ? "Estado: DETENIDO     "
                    :                      "Estado: Reproduciendo";
    vga_print(7, 0, txt, C_YELLOW, C_BLUE);
}

// Actualiza el indicador de estado en la VGA si cambió
static void vga_poll_state(volatile uint32_t *sh) {
    uint32_t st = sh[IDX_STATE];
    if (st != last_vga_state) { last_vga_state = st; vga_show_state(st); }
}

typedef struct {
    FILE    *wav;
    uint16_t channels;
    uint32_t total_frames;
} reader_arg_t;

// Analiza el encabezado de un archivo WAV y extrae metadatos
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
        long next = ftell(f) + csize + (csize & 1);

        if (memcmp(tag, "fmt ", 4) == 0) {
            fread(&u16, 2, 1, f);
            fread(channels_out, 2, 1, f);
            fread(sample_rate_out, 4, 1, f);
            fread(&u32, 4, 1, f);
            fread(&u16, 2, 1, f);
            fread(bits_out, 2, 1, f);
            have_fmt = 1;
        }
        else if (memcmp(tag, "data", 4) == 0) {
            *data_size_out = csize;
            return have_fmt ? 0 : -1;
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
                    fseek(f, (ss - n) + (ss & 1), SEEK_CUR);
                }
            }
        }
        fseek(f, next, SEEK_SET);
    }
    return -1;
}

// Comparador para qsort que ordena cadenas alfabéticamente
static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Escanea un directorio y construye una lista de archivos WAV
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

// Hilo lector que carga audio desde SD a un buffer circular en RAM
static void *reader_thread(void *arg) {
    reader_arg_t *ra = (reader_arg_t *)arg;
    uint32_t remaining   = ra->total_frames;
    uint32_t frame_bytes = ra->channels * 2;

    while (remaining > 0 && !reader_stop) {
        uint32_t want = (remaining < CHUNK_FRAMES) ? remaining : CHUNK_FRAMES;

        while (!reader_stop) {
            uint32_t used = (ring_head - ring_tail + RING_FRAMES) % RING_FRAMES;
            if ((RING_FRAMES - 1 - used) >= want) break;
            usleep(1000);
        }
        if (reader_stop) break;

        size_t got = fread(chunk, frame_bytes, want, ra->wav);
        if (got == 0) break;

        uint32_t h = ring_head;
        for (size_t j = 0; j < got; j++) {
            int16_t sl, sr;
            if (ra->channels == 2) { sl = chunk[2 * j]; sr = chunk[2 * j + 1]; }
            else                   { sl = chunk[j];     sr = sl; }
            ring[h] = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;
            h = (h + 1) % RING_FRAMES;
        }
        __sync_synchronize();
        ring_head = h;
        remaining -= got;
    }
    reader_done = 1;
    return NULL;
}

// Reproduce una pista de audio del archivo WAV especificado
static int play_track(volatile uint32_t *sh, const char *fname, int num, int total) {
    FILE *wav = fopen(fname, "rb");
    if (!wav) { perror("fopen"); return REQ_NEXT; }

    uint16_t channels, bits;
    uint32_t sample_rate, data_size;
    char title[64] = {0}, artist[64] = {0}, album[64] = {0};
    if (parse_wav(wav, &channels, &sample_rate, &bits, &data_size,
                  title, artist, album) < 0 || bits != 16) {
        fprintf(stderr, "WAV no soportado (no 16-bit?): %s\n", fname);
        fclose(wav); return REQ_NEXT;
    }

    sh[IDX_CMD] = CMD_STOP;
    for (volatile int w = 0; w < 2000000 && sh[IDX_CMD] != CMD_IDLE; w++) {}
    sh[IDX_HEAD] = 0;
    sh[IDX_TAIL] = 0;
    sh[IDX_REQ]  = REQ_NONE;
    uint32_t head = 0;
    sh[IDX_CMD]  = CMD_PLAY;

    uint32_t dur_sec = sample_rate ? (data_size / (channels * 2)) / sample_rate : 0;

    printf("Reproduciendo: %s (%u Hz, %u ch, %u s)\n", fname, sample_rate, channels, dur_sec);
    vga_show_track(title, artist, album, fname, num, total, dur_sec);
    last_vga_state = 0xFFFFFFFF;
    vga_poll_state(sh);

    reader_stop = 0;
    reader_done = 0;
    ring_head = 0;
    ring_tail = 0;
    reader_arg_t ra = { wav, channels, data_size / (channels * 2) };
    pthread_t tid;
    pthread_create(&tid, NULL, reader_thread, &ra);

    int      ret = REQ_NONE;
    uint32_t pub = 0;

    for (;;) {
        vga_poll_state(sh);
        uint32_t rh = ring_head;
        __sync_synchronize();

        if (ring_tail == rh) {
            if (reader_done) break;
            if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; goto stop; }
            continue;
        }

        while (ring_tail != rh) {
            uint32_t packed = ring[ring_tail];

            uint32_t next = (head + 1) % BUF_WORDS;
            while (next == sh[IDX_TAIL]) {
                vga_poll_state(sh);
                if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; goto stop; }
            }

            sh[BUF_WORD_START + head] = packed;
            head = next;
            ring_tail = (ring_tail + 1) % RING_FRAMES;

            if (++pub >= 512) { sh[IDX_HEAD] = head; pub = 0; }
        }
        sh[IDX_HEAD] = head;
    }

    sh[IDX_HEAD] = head;
    while (sh[IDX_TAIL] != head) {
        if (sh[IDX_REQ] != REQ_NONE) { ret = sh[IDX_REQ]; sh[IDX_REQ] = REQ_NONE; break; }
    }

stop:
    reader_stop = 1;
    pthread_join(tid, NULL);
    fclose(wav);
    return ret;
}

// Función principal del reproductor de audio
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
    vga = (volatile uint32_t *)((char *)lw + VGA_OFFSET);

    sh[IDX_CMD] = CMD_IDLE;
    sh[IDX_REQ] = REQ_NONE;

    int idx = 0;
    while (1) {
        int r = play_track(sh, playlist[idx], idx + 1, n_tracks);
        if      (r == REQ_PREV) idx = (idx - 1 + n_tracks) % n_tracks;
        else if (r == REQ_STOP) { }
        else                    idx = (idx + 1) % n_tracks;
    }

    munmap((void *)lw, MAP_SIZE);
    close(fd);
    return 0;
}