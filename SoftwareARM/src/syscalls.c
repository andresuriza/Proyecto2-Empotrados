#include <sys/stat.h>
#include <stdint.h>
#include <stddef.h>

// ── mem* byte a byte (override de newlib) ─────────────────────────────
// La MMU esta desactivada bajo U-Boot -> toda la memoria es device/strongly-
// ordered, donde los accesos de PALABRA no alineados SIEMPRE fallan (data
// abort), sin importar SCTLR.A. newlib memcpy/memset hacen accesos de palabra
// no alineados. Aqui los reemplazamos por versiones byte-a-byte, seguras.
// (Compilado con -fno-tree-loop-distribute-patterns para que GCC no convierta
//  estos bucles en llamadas a memcpy/memset y se autollame en bucle.)
void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

// HPS UART0 (DesignWare 16550) — consola serial de la DE1-SoC (la de socfpga)
#define UART_THR  (*(volatile unsigned int*)0xFFC02000)          // dato TX/RX (offset 0x00)
#define UART_LSR  (*(volatile unsigned int*)(0xFFC02000 + 0x14)) // Line Status Register
#define LSR_THRE  (1 << 5)   // Transmit Holding Empty -> listo para transmitir
#define LSR_DR    (1 << 0)   // Data Ready -> hay byte para recibir

// printf → escribe al UART esperando que el TX este listo.
// Traduce '\n' -> '\r\n' para que la consola serial no escalone las lineas
// (un LF solo baja de linea pero no vuelve a la columna 0).
int _write(int fd, char* buf, int len) {
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') {
            while (!(UART_LSR & LSR_THRE));
            UART_THR = '\r';
        }
        while (!(UART_LSR & LSR_THRE));   // esperar TX listo
        UART_THR = (unsigned char)c;
    }
    return len;
}

// getchar → lee del UART esperando que llegue un byte
int _read(int fd, char* buf, int len) {
    while (!(UART_LSR & LSR_DR));          // esperar hasta que haya dato
    buf[0] = (char)(UART_THR & 0xFF);
    return 1;
}

// Heap para malloc
static uint8_t heap[8192];
static uint8_t* heap_ptr = heap;
void* _sbrk(int incr) {
    uint8_t* prev = heap_ptr;
    heap_ptr += incr;
    return (void*)prev;
}

// Stubs vacíos
int  _close(int fd)                  { return -1; }
int  _fstat(int fd, struct stat* st) { return 0; }
int  _isatty(int fd)                 { return 1; }
int  _lseek(int fd, int ptr, int dir){ return 0; }
int  _getpid(void)                   { return 1; }
int  _kill(int pid, int sig)         { return -1; }
void _exit(int status)               { while(1); }