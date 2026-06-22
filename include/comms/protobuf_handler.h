#ifndef PROTOBUF_HANDLER_H
#define PROTOBUF_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "meshtastic/mesh.pb.h"

// Decode functions
meshtastic_FromRadio *decode_from_radio(const uint8_t *data, size_t len);

// Send functions
int send_meshtastic_frame(const uint8_t *payload, size_t payload_len);
int send_want_config(void);
int send_text_message(uint32_t target_node, const char *text, size_t text_len);

#endif