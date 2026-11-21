/* rootclock simulator: the two UART calls common/rc_link.h makes. The only
 * UART is the inter-CPU link, which sim_link.c connects to main's bridge. */
#ifndef ROOTCLOCK_SIM_HARDWARE_UART_H
#define ROOTCLOCK_SIM_HARDWARE_UART_H
#include <stdbool.h>
#include <stdint.h>
typedef struct sim_uart uart_inst_t;
#define uart0 ((uart_inst_t *)0)
bool uart_is_writable(uart_inst_t *u);
void uart_putc_raw(uart_inst_t *u, char c);
#endif
