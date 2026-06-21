#ifndef COMMON_H
#define COMMON_H

// Meshtastic specific globals
#define MAX_MESSAGE_HISTORY 300
#define MAX_NODE_HISTORY 300
#define MAX_MESSAGE_TEXT_LENGTH 256
#define WAIT_FOR_NODE_INFO_TIMEOUT_MS 30000


// Ring buffer
#define RING_BUFFER_SIZE 16


// UART HAL
#define UART_DEFAULT_BAUD_RATE 115200

// Message processor task
#define MSG_TASK_STACK_SIZE 4096




#endif