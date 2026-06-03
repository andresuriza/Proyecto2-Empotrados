#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#ifdef TARGET_SIMULATION
  #define UART0_BASE   0x10009000
  #define UART_DR      (*(volatile uint32_t*)(UART0_BASE + 0x000))
  #define UART_FR      (*(volatile uint32_t*)(UART0_BASE + 0x018))
  #define UART_FR_RXFE (1 << 4)
#endif

#endif