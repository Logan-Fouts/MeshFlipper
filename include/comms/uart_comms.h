#ifndef UART_COMMS_H
#define UART_COMMS_H

#include "hardware/uart_hal.h"
#include "models/ring_buffer.h"

// UART communication module that handles framing, parsing, and statistics for UART messages.
typedef struct {
    uart_hal_t *hal;
    
    // RX state machine
    enum {
        RX_WAIT_START1,
        RX_WAIT_START2,
        RX_WAIT_LEN_MSB,
        RX_WAIT_LEN_LSB,
        RX_READ_PAYLOAD,
    } rx_state;
    
    uint8_t rx_frame[512 + 4];
    size_t rx_pos;
    size_t rx_expected_len;
    
    // Stats
    struct {
        size_t frames_received;
        size_t frames_dropped;
        size_t bytes_processed;
        size_t decode_errors;
    } stats;
    
    bool initialized;
} uart_comms_t;

// Global instance
extern uart_comms_t g_uart_comms;

int uart_comms_init(uart_comms_t *comms, uart_hal_t *hal);
int uart_comms_send_frame(uart_comms_t *comms, const uint8_t *payload, size_t payload_len);
void uart_comms_process_byte(uint8_t byte, void *context);
void uart_comms_reset(uart_comms_t *comms);

// Statistics
size_t uart_comms_get_frames_received(uart_comms_t *comms);
size_t uart_comms_get_frames_dropped(uart_comms_t *comms);
size_t uart_comms_get_bytes_processed(uart_comms_t *comms);
void uart_comms_reset_stats(uart_comms_t *comms);

#endif