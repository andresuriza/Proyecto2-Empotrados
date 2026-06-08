#include <sys/stat.h>
#include <stdint.h>

// HPS UART0 (DesignWare 16550) — consola serial de la DE1-SoC (la de socfpga)
#define UART_THR  (*(volatile unsigned int*)0xFFC02000)          // dato TX/RX (offset 0x00)
#define UART_LSR  (*(volatile unsigned int*)(0xFFC02000 + 0x14)) // Line Status Register
#define LSR_THRE  (1 << 5)   // Transmit Holding Empty -> listo para transmitir
#define LSR_DR    (1 << 0)   // Data Ready -> hay byte para recibir

// printf → escribe al UART esperando que el TX este listo
int _write(int fd, char* buf, int len) {
    for (int i = 0; i < len; i++) {
        while (!(UART_LSR & LSR_THRE));   // esperar TX listo
        UART_THR = (unsigned char)buf[i];
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