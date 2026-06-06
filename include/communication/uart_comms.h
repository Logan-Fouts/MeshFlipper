#ifndef UART_COMMS_H
#define UART_COMMS_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/drivers/uart.h>
#include "meshtastic/mesh.pb.h"
#include "communication/manage_pb.h"

int send_meshtastic_frame(const uint8_t *payload, size_t payload_len);
int send_want_config(void);
void uart_cb(const struct device *dev, void *user_data);

extern volatile size_t rx_frames_received;
extern volatile size_t rx_frames_dropped;
extern volatile size_t rx_bytes_processed;

#endif
