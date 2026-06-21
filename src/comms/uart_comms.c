#include "comms/uart_comms.h"
#include "comms/protobuf_handler.h"
#include "models/ring_buffer.h"
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>

#define MESHTASTIC_START1 0x94
#define MESHTASTIC_START2 0xC3
#define RX_FRAME_MAX 512

// Define the global instance
uart_comms_t g_uart_comms = {
    .hal = NULL,
    .rx_state = RX_WAIT_START1,
    .rx_pos = 0,
    .rx_expected_len = 0,
    .initialized = false,
    .stats = {0}
};

extern ring_buffer_t g_msg_ring_buffer;

// Initializes the UART comms module with the specified UART HAL. Returns 0 on success, -1 on failure.
int uart_comms_init(uart_comms_t *comms, uart_hal_t *hal)
{
    if (!comms || !hal) return -EINVAL;
    
    comms->hal = hal;
    comms->rx_state = RX_WAIT_START1;
    comms->rx_pos = 0;
    comms->rx_expected_len = 0;
    comms->initialized = true;
    memset(&comms->stats, 0, sizeof(comms->stats)); // Clear stats
    
    return 0;
}

void uart_comms_reset(uart_comms_t *comms)
{
    if (comms) {
        comms->rx_state = RX_WAIT_START1;
        comms->rx_pos = 0;
        comms->rx_expected_len = 0;
    }
}

static meshtastic_FromRadio *rx_consume_byte(uart_comms_t *comms, uint8_t c)
{
    if (!comms) return NULL;
    
    // If we see a new frame start while in the middle of a frame,
    // restart the frame capture (this handles lost sync)
    if (c == MESHTASTIC_START1 && comms->rx_state != RX_WAIT_START1) {
        if (comms->rx_state == RX_READ_PAYLOAD && 
            (comms->rx_pos - 4) > 10) {
            // We're mid-frame and saw a new start - probably lost sync
            comms->rx_frame[0] = c;
            comms->rx_pos = 1;
            comms->rx_state = RX_WAIT_START2;
            return NULL;
        }
    }
    
    switch (comms->rx_state) {
        case RX_WAIT_START1:
            if (c == MESHTASTIC_START1) {
                comms->rx_frame[0] = c;
                comms->rx_pos = 1;
                comms->rx_state = RX_WAIT_START2;
            }
            break;
        
        case RX_WAIT_START2:
            if (c == MESHTASTIC_START2) {
                comms->rx_frame[comms->rx_pos++] = c;
                comms->rx_state = RX_WAIT_LEN_MSB;
            } else if (c == MESHTASTIC_START1) {
                // Restart with new frame
                comms->rx_frame[0] = c;
                comms->rx_pos = 1;
            } else {
                uart_comms_reset(comms);
            }
            break;
        
        case RX_WAIT_LEN_MSB:
            comms->rx_frame[comms->rx_pos++] = c;
            comms->rx_expected_len = ((size_t)c) << 8;
            comms->rx_state = RX_WAIT_LEN_LSB;
            break;

        case RX_WAIT_LEN_LSB:
            comms->rx_frame[comms->rx_pos++] = c;
            comms->rx_expected_len |= c;

            if (comms->rx_expected_len == 0 || comms->rx_expected_len > RX_FRAME_MAX) {
                printk("Invalid Meshtastic frame length: %u\n", (unsigned int)comms->rx_expected_len);
                uart_comms_reset(comms);
                break;
            }

            comms->rx_pos = 4;
            comms->rx_state = RX_READ_PAYLOAD;
            break;

        case RX_READ_PAYLOAD:
            comms->rx_frame[comms->rx_pos++] = c;

            if ((comms->rx_pos - 4) >= comms->rx_expected_len) {
                meshtastic_FromRadio *msg = decode_from_radio(&comms->rx_frame[4], comms->rx_expected_len);
                uart_comms_reset(comms);
                
                if (msg != NULL) {
                    return msg;
                } else {
                    // Decode failed - count it but continue
                    comms->stats.decode_errors++;
                }
            }
            break;
    }
    return NULL;
}

int uart_comms_send_frame(uart_comms_t *comms, const uint8_t *payload, size_t payload_len)
{
    if (!comms || !comms->initialized || !payload) return -EINVAL;
    if (payload_len == 0 || payload_len > RX_FRAME_MAX) return -EINVAL;
    
    uint8_t header[4] = {
        MESHTASTIC_START1,
        MESHTASTIC_START2,
        (uint8_t)((payload_len >> 8) & 0xFF),
        (uint8_t)(payload_len & 0xFF)
    };
    
    int ret = uart_hal_send_bytes(comms->hal, header, sizeof(header));
    if (ret != 0) return ret;
    
    return uart_hal_send_bytes(comms->hal, payload, payload_len);
}

// ===============================
// MAIN BYTE PROCESSING FUNCTION 
// ===============================

void uart_comms_process_byte(uint8_t byte, void *context)
{
    uart_comms_t *comms = (uart_comms_t*)context;
    if (!comms || !comms->initialized) return;
    
    // Update statistics
    comms->stats.bytes_processed++;
    
    // Check for buffer overflow in RX state
    if (comms->rx_state == RX_READ_PAYLOAD && comms->rx_pos >= sizeof(comms->rx_frame)) {
        comms->stats.frames_dropped++;
        uart_comms_reset(comms);
        return;
    }
    
    // Process the byte through state machine
    meshtastic_FromRadio *msg = rx_consume_byte(comms, byte);
    if (msg == NULL) {
        return;
    }
    
    // Frame received successfully
    comms->stats.frames_received++;
    
    // Add to ring buffer (this handles the copy and semaphore)
    if (!ring_buffer_put(&g_msg_ring_buffer, msg)) {
        comms->stats.frames_dropped++;
        printk("WARNING: Ring buffer full - dropping frame\n");
    }
}

// ============================================================
// STATISTICS AND UTILITY FUNCTIONS
// ============================================================

size_t uart_comms_get_frames_received(uart_comms_t *comms)
{
    return comms ? comms->stats.frames_received : 0;
}

size_t uart_comms_get_frames_dropped(uart_comms_t *comms)
{
    return comms ? comms->stats.frames_dropped : 0;
}

size_t uart_comms_get_bytes_processed(uart_comms_t *comms)
{
    return comms ? comms->stats.bytes_processed : 0;
}

void uart_comms_reset_stats(uart_comms_t *comms)
{
    if (comms) {
        memset(&comms->stats, 0, sizeof(comms->stats));
    }
}