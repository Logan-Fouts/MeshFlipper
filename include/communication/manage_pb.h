#ifndef MANAGE_PB_H
#define MANAGE_PB_H

#include <stdint.h>
#include <stddef.h>
#include "meshtastic/mesh.pb.h"

meshtastic_FromRadio *decode_from_radio(const uint8_t *payload, size_t len);

#endif
