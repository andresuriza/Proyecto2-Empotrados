#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#define LW_BRIDGE_BASE   0xFF200000UL
#define VGA_OFFSET       0x00010000UL
#define MAP_SPAN         0x00020000UL

// Índices de los registros del controlador VGA
#define REG_BG_COLOR     0
#define REG_MODE         1

// Modos de operación soportados por el controlador
#define MODE_SOLID       0
#define MODE_BARS        1
#define MODE_CHECKER     2

// Empaqueta componentes RGB de 8 bits en un valor de 24 bits
#define RGB(r, g, b)     (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

// Escribe un registro del controlador VGA y verifica la operación.
static void reg_write_verify(volatile uint32_t *reg,
                             uint32_t val,
                             uint32_t mask,
                             const char *name)
{
    // Escribir el nuevo valor en el registro
    *reg = val;

    // Garantizar que la escritura llegue al hardware antes de continuar
    __sync_synchronize();

    // Leer nuevamente el registro para verificar la operación
    uint32_t rb = *reg;

    const char *flag =
        ((rb & mask) == (val & mask))
            ? "OK"
            : "MISMATCH (write not reaching FPGA?)";

    printf("    %-8s <= 0x%06X   read-back 0x%08X  [%s]\n",
           name, val, rb, flag);
}

// Ejecuta una prueba funcional del controlador VGA
int main(void)
{
    // Obtener acceso a la memoria física del sistema
    int fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (fd < 0)
    {
        perror("open /dev/mem (run as root?)");
        return 1;
    }

    // Calcular una dirección base alineada a página para cumplir con los requisitos de mmap()
    long pagesize = sysconf(_SC_PAGE_SIZE);

    unsigned long aligned_base =
        LW_BRIDGE_BASE & ~((unsigned long)pagesize - 1UL);

    unsigned long in_page =
        LW_BRIDGE_BASE - aligned_base;

    size_t span = in_page + MAP_SPAN;

    // Mapear el puente LW en el espacio virtual del proceso
    void *map =
        mmap(NULL,
             span,
             PROT_READ | PROT_WRITE,
             MAP_SHARED,
             fd,
             (off_t)aligned_base);

    if (map == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 1;
    }

    // Obtener un puntero al controlador VGA dentro de la región previamente mapeada.
    volatile uint32_t *vga =
        (volatile uint32_t *)((char *)map +
                              in_page +
                              VGA_OFFSET);

    // Mostrar información útil para validar el mapa de memoria
    printf("Bridge base   = 0x%08lX (page-aligned 0x%08lX, span 0x%zX)\n",
           LW_BRIDGE_BASE,
           aligned_base,
           span);

    printf("VGA base      = 0x%08lX  (bridge + 0x%05lX)\n",
           LW_BRIDGE_BASE + VGA_OFFSET,
           VGA_OFFSET);

    printf("BG_COLOR addr = 0x%08lX  (word 0, byte 0x0)\n",
           LW_BRIDGE_BASE + VGA_OFFSET + 0x0);

    printf("MODE addr     = 0x%08lX  (word 1, byte 0x4)\n",
           LW_BRIDGE_BASE + VGA_OFFSET + 0x4);

    // Pantalla completamente roja
    printf("[1/6] MODE=solid, BG=RED   -> full red screen\n");

    reg_write_verify(
        &vga[REG_MODE],
        MODE_SOLID,
        0x3,
        "MODE");

    reg_write_verify(
        &vga[REG_BG_COLOR],
        RGB(0xFF, 0x00, 0x00),
        0xFFFFFF,
        "BG_COLOR");

    sleep(2);

    // Pantalla completamente verde.
    printf("[2/6] MODE=solid, BG=GREEN -> full green screen\n");

    reg_write_verify(
        &vga[REG_BG_COLOR],
        RGB(0x00, 0xFF, 0x00),
        0xFFFFFF,
        "BG_COLOR");

    sleep(2);

    // Pantalla completamente azul.
    printf("[3/6] MODE=solid, BG=BLUE  -> full blue screen\n");

    reg_write_verify(
        &vga[REG_BG_COLOR],
        RGB(0x00, 0x00, 0xFF),
        0xFFFFFF,
        "BG_COLOR");

    sleep(2);

    // Barras verticales de colores
    printf("[4/6] MODE=bars            -> 8 vertical colour bars\n");

    reg_write_verify(
        &vga[REG_MODE],
        MODE_BARS,
        0x3,
        "MODE");

    sleep(2);

    // Mostrar patrón checkerboard.
    printf("[5/6] MODE=checkerboard    -> checkerboard pattern\n");

    reg_write_verify(
        &vga[REG_MODE],
        MODE_CHECKER,
        0x3,
        "MODE");

    sleep(2);

    // Restaurar el estado inicial del controlador.
    printf("[6/6] MODE=solid, BG=BLACK -> done\n");

    reg_write_verify(
        &vga[REG_MODE],
        MODE_SOLID,
        0x3,
        "MODE");

    reg_write_verify(
        &vga[REG_BG_COLOR],
        0x000000,
        0xFFFFFF,
        "BG_COLOR");

    // Liberar los recursos asociados al mapeo de memoria
    if (munmap(map, span) != 0)
        perror("munmap");

    close(fd);

    printf("VGA test complete.\n");

    return 0;
}