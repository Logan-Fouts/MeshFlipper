#include "models/ring_buffer.h"
#include <string.h>


void ring_buffer_init(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    
    memset(rb->buffer, 0, sizeof(rb->buffer));
    rb->write_idx = 0;
    rb->read_idx = 0;
    rb->dropped_count = 0;
    
    // Initialize semaphore with 0 initial count, max count = RING_BUFFER_SIZE
    k_sem_init(&rb->semaphore, 0, RING_BUFFER_SIZE);
}

bool ring_buffer_put(ring_buffer_t *rb, const meshtastic_FromRadio *msg)
{
    if (rb == NULL || msg == NULL) {
        return false;
    }
    
    int next = (rb->write_idx + 1) % RING_BUFFER_SIZE;
    
    if (next == rb->read_idx) {
        rb->dropped_count++;
        printf("Ring buffer full, dropping message. Total dropped: %zu\n", rb->dropped_count);
        return false;
    }
    
    // Copy message into buffer
    memcpy(&rb->buffer[rb->write_idx], msg, sizeof(meshtastic_FromRadio));
    
    // Update write index
    rb->write_idx = next;
    
    // Signal that a new message is available
    k_sem_give(&rb->semaphore);
    
    return true;
}

bool ring_buffer_get(ring_buffer_t *rb, meshtastic_FromRadio *msg)
{
    if (rb == NULL || msg == NULL) {
        return false;
    }
    
    if (rb->read_idx == rb->write_idx) {
        return false;
    }
    
    // Copy message from buffer
    memcpy(msg, &rb->buffer[rb->read_idx], sizeof(meshtastic_FromRadio));
    
    // Update read index
    rb->read_idx = (rb->read_idx + 1) % RING_BUFFER_SIZE;
    
    return true;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return true;
    }
    return (rb->read_idx == rb->write_idx);
}

bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return false;
    }
    int next = (rb->write_idx + 1) % RING_BUFFER_SIZE;
    return (next == rb->read_idx);
}

size_t ring_buffer_available(const ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    
    if (rb->write_idx >= rb->read_idx) {
        return rb->write_idx - rb->read_idx;
    } else {
        return RING_BUFFER_SIZE - (rb->read_idx - rb->write_idx);
    }
}

size_t ring_buffer_get_dropped(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    return rb->dropped_count;
}

void ring_buffer_reset_dropped(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    rb->dropped_count = 0;
}

bool ring_buffer_wait(ring_buffer_t *rb, k_timeout_t timeout)
{
    if (rb == NULL) {
        return false;
    }
    return (k_sem_take(&rb->semaphore, timeout) == 0);
}