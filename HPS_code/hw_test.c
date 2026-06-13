#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LW_BRIDGE_BASE  0xFF200000UL
#define MAP_SIZE        0x80000
#define SHARED_OFFSET   0x00000

// Indices utilizados para la comunicación ARM–Nios II
#define IDX_CMD     0
#define IDX_HEAD    1
#define IDX_TAIL    2

// Comienzo y tamaño del buffer circular compartido
#define BUF_WORD_START  64
#define BUF_WORDS       16320

// Analiza la cabecera de un archivo WAV.
static int parse_wav(FILE *f,
                     uint16_t *channels_out,
                     uint32_t *sample_rate_out,
                     uint16_t *bits_out,
                     uint32_t *data_size_out)
{
    char tag[4];
    uint32_t u32;
    uint16_t u16;

    // Verificar encabezados RIFF y WAVE
    fread(tag, 4, 1, f);
    if (memcmp(tag, "RIFF", 4))
        return -1;

    fread(&u32, 4, 1, f);

    fread(tag, 4, 1, f);
    if (memcmp(tag, "WAVE", 4))
        return -1;

    // Buscar el bloque de formato
    while (!feof(f))
    {
        fread(tag, 4, 1, f);
        fread(&u32, 4, 1, f);

        if (memcmp(tag, "fmt ", 4) == 0)
        {
            // Extraer parámetros de audio PCM
            fread(&u16, 2, 1, f);
            fread(channels_out, 2, 1, f);
            fread(sample_rate_out, 4, 1, f);
            fread(&u32, 4, 1, f);
            fread(&u16, 2, 1, f);
            fread(bits_out, 2, 1, f);
            break;
        }

        // Ignorar bloques no relevantes
        fseek(f, u32, SEEK_CUR);
    }

    // Buscar el bloque que contiene las muestras PCM
    while (!feof(f))
    {
        fread(tag, 4, 1, f);
        fread(data_size_out, 4, 1, f);

        if (memcmp(tag, "data", 4) == 0)
            return 0;

        // Saltar bloques que no contienen audio
        fseek(f, *data_size_out, SEEK_CUR);
    }

    return -1;
}

// Reproduce un archivo WAV utilizando memoria compartida
int main(int argc, char *argv[])
{
    // Verificar que se haya proporcionado un archivo WAV
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <archivo.wav>\n", argv[0]);
        return 1;
    }

    // Abrir el archivo de audio
    FILE *wav = fopen(argv[1], "rb");
    if (!wav)
    {
        perror("fopen");
        return 1;
    }

    uint16_t channels;
    uint16_t bits;
    uint32_t sample_rate;
    uint32_t data_size;

    // Obtener información de la cabecera WAV
    if (parse_wav(
            wav,
            &channels,
            &sample_rate,
            &bits,
            &data_size) < 0)
    {
        fprintf(stderr, "Error al parsear WAV\n");
        fclose(wav);
        return 1;
    }

    printf("WAV: %u Hz, %u canales, %u bits, %u bytes de data\n",
           sample_rate,
           channels,
           bits,
           data_size);

    // Validar que el formato de audio sea soportado
    if (bits != 16)
    {
        fprintf(stderr, "Solo se soportan WAV de 16 bits\n");
        fclose(wav);
        return 1;
    }

    // Obtener acceso a la memoria física del sistema
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        perror("open /dev/mem");
        fclose(wav);
        return 1;
    }

    // Mapear el LW Bridge en el espacio virtual del proceso
    volatile uint32_t *lw =
        mmap(NULL,
             MAP_SIZE,
             PROT_READ | PROT_WRITE,
             MAP_SHARED,
             fd,
             LW_BRIDGE_BASE);

    if (lw == MAP_FAILED)
    {
        perror("mmap");
        fclose(wav);
        close(fd);
        return 1;
    }

    // Obtener acceso a la región de memoria compartida
    volatile uint32_t *sh =
        (volatile uint32_t *)((char *)lw + SHARED_OFFSET);

    // Inicializar el mailbox utilizado para sincronizar la transferencia de audio entre ARM y Nios II.
    sh[IDX_CMD]  = 0;
    sh[IDX_HEAD] = 0;
    sh[IDX_TAIL] = 0;

    uint32_t head = 0;

    // Notificar al Nios II el inicio de la reproducción
    sh[IDX_CMD] = 1;

    printf("Reproduciendo...\n");

    // Calcular la cantidad total de frames presentes en el archivo. Cada frame contiene una muestra por canal.
    uint32_t total_frames = data_size / (channels * 2);

    for (uint32_t i = 0; i < total_frames; i++)
    {
        int16_t sl = 0;
        int16_t sr = 0;

        // Leer la siguiente muestra del archivo
        fread(&sl, 2, 1, wav);

        if (channels == 2)
        {
            // Leer el canal derecho en archivos estéreo
            fread(&sr, 2, 1, wav);
        }
        else
        {
            // Duplicar la muestra mono para generar una salida estéreo
            sr = sl;
        }

        // Empaquetar ambos canales (L/R) en una única palabra
        uint32_t packed =
            ((uint32_t)(uint16_t)sl << 16) |
            (uint16_t)sr;

        // Esperar mientras el buffer circular esté lleno. El Nios II avanzará IDX_TAIL conforme consuma 
        // muestras para el periférico de audio
        uint32_t next = (head + 1) % BUF_WORDS;

        while (next == sh[IDX_TAIL])
        {
        }

        // Escribir la muestra en la memoria compartida
        sh[BUF_WORD_START + head] = packed;

        // Avanzar la posición de escritura
        head = next;

        // Publicar la nueva cabeza del buffer circular
        sh[IDX_HEAD] = head;
    }

    // Informar al Nios II que no quedan más muestras por enviar
    sh[IDX_CMD] = 2;

    printf("Listo.\n");

    // Liberar recursos utilizados por la aplicación
    munmap((void *)lw, MAP_SIZE);
    close(fd);
    fclose(wav);

    return 0;
}