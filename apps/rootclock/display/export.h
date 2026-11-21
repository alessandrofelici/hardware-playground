/* rootclock -- display CPU: getting finished games off the board (004).
 *
 * At GAME OVER the result goes into a small outbox in RAM (core/rc_uplink.h)
 * and the WiFi module is asked to connect. Yellow (SEND) hands the oldest
 * result to the main CPU over the inter-CPU link; main passes it to the
 * Bottlenose ESP32-C6, which saves it to its own flash and then uploads it to
 * root-metrics. A result leaves the outbox only once the C6 says it saved it
 * (or the server already has it), so a missing module, no WiFi or a power-off
 * mid-transfer never loses a game: it stays here, and yellow tries again.
 * One press hands over every waiting result in turn.
 *
 * Outside GAME OVER the WiFi module is told to switch off once it has
 * nothing left to upload. */
#ifndef EXPORT_H
#define EXPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rc_types.h"
#include "rc_uplink.h"

/* Brings the link UART up (the bootloader leaves it down). */
void export_init(uint32_t now_ms);
/* A new result (the contract-v1 JSON). */
void export_add(const char *json, size_t len);
/* Yellow in GAME OVER. */
void export_send(uint32_t now_ms);
/* Every loop iteration, with the game state: link I/O, retries, WiFi on in
 * OVER and off elsewhere. */
void export_poll(rc_state_t state, uint32_t now_ms);

/* For the screen and the test console. */
void export_view(rc_uplink_view_t *v, uint32_t now_ms);
uint8_t export_waiting(void);   /* results not yet handed to the C6 */

#endif
