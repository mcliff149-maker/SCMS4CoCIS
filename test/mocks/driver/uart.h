#ifndef TEST_UART_H
#define TEST_UART_H
#include <stddef.h>
#define UART_NUM_0 0
int uart_read_bytes(int port, void *buffer, size_t size, unsigned int ticks);
#endif
