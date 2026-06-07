#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "meshtastic/mesh.pb.h"

#define RING_BUFFER_SIZE 16

typedef struct {
    meshtastic_FromRadio buffer[RING_BUFFER_SIZE];
    int write_idx;
    int read_idx;
    struct k_sem semaphore;
    size_t dropped_count;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *rb);
bool ring_buffer_put(ring_buffer_t *rb, const meshtastic_FromRadio *msg);
bool ring_buffer_get(ring_buffer_t *rb, meshtastic_FromRadio *msg);
bool ring_buffer_is_empty(const ring_buffer_t *rb);
bool ring_buffer_is_full(const ring_buffer_t *rb);
size_t ring_buffer_available(const ring_buffer_t *rb);
size_t ring_buffer_get_dropped(ring_buffer_t *rb);
void ring_buffer_reset_dropped(ring_buffer_t *rb);
bool ring_buffer_wait(ring_buffer_t *rb, k_timeout_t timeout);

#endif 