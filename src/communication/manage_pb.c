#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include "communication/manage_pb.h"

// Decodes a complete Meshtastic frame payload using nanopb.
meshtastic_FromRadio *decode_from_radio(const uint8_t *payload, size_t len)
{
    // Keep the decoded message in static storage because the caller uses the pointer after this function returns.
    static meshtastic_FromRadio msg;
    memset(&msg, 0, sizeof(msg));
    pb_istream_t stream = pb_istream_from_buffer(payload, len);

    // Decode the payload into the FromRadio message struct. If decoding fails, print an error and return.
    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg)) {
        printk("FromRadio decode failed: %s\n", PB_GET_ERROR(&stream));
        return NULL;
    }

    return &msg;
} 