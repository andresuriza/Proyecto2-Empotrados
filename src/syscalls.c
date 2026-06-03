#include <sys/stat.h>
#include <stdint.h>

// PL011 UART - vexpress-a9
#define UART_DR  (*(volatile unsigned int*)0x10009000)
#define UART_FR  (*(volatile unsigned int*)0x10009018)
#define UART_RXFE (1 << 4)   // Receive FIFO empty

// printf → escribe al UART sin verificar TXFF
int _write(int fd, char* buf, int len) {
    for (int i = 0; i < len; i++) {
        UART_DR = buf[i];
    }
    return len;
}

// getchar → lee del UART esperando que llegue un byte
int _read(int fd, char* buf, int len) {
    while (UART_FR & UART_RXFE);     // esperar hasta que haya dato
    buf[0] = (char)(UART_DR & 0xFF);
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