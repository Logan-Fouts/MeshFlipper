#include "tasks/message_processor.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"
#include "models/ring_buffer.h"
#include "comms/protobuf_handler.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <errno.h>

// =============
// STATIC DATA
// =============

static ring_buffer_t *g_rx_queue = NULL;
static struct messageHistory *g_message_history = NULL;
static struct nodeHistory *g_node_list = NULL;

#define MSG_TASK_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(g_msg_task_stack, MSG_TASK_STACK_SIZE);
static struct k_thread g_msg_task_thread;

static struct {
    uint32_t messages_processed;
    uint32_t errors;
    uint32_t ui_updates;
} g_stats = {0};

// ==================
// STATIC FUNCTIONS
// ==================

static void process_message(const meshtastic_FromRadio *msg)
{
    if (!msg) return;
    
    
    // Handle queue status messages
    if (msg->which_payload_variant == meshtastic_FromRadio_queueStatus_tag) {
        printk("QueueStatus: res=%d free=%u/%u mesh_packet_id=%u\n",
               (int)msg->queueStatus.res,
               (unsigned int)msg->queueStatus.free,
               (unsigned int)msg->queueStatus.maxlen,
               (unsigned int)msg->queueStatus.mesh_packet_id);
        return;
    }
    
    // Handle node info updates
    if (msg->which_payload_variant == meshtastic_FromRadio_my_info_tag ||
        msg->which_payload_variant == meshtastic_FromRadio_node_info_tag) {
        update_node_history(g_node_list, msg);
        
        if (msg->which_payload_variant == meshtastic_FromRadio_my_info_tag) {
            printk("✅ My info received! Node num: %u\n", (unsigned int)g_node_list->my_info.num);
        }
        return;
    }
    
    // Handle text messages
    if (msg->which_payload_variant == meshtastic_FromRadio_packet_tag &&
        msg->packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        msg->packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        
        if (g_node_list->my_info.valid && 
            msg->packet.from == g_node_list->my_info.num) {
            return;
        }
        
        update_message_history(g_message_history, msg);
        g_stats.messages_processed++;
        printk("📩 New message from %u: %s\n", 
               (unsigned int)msg->packet.from, 
               msg->packet.decoded.payload.bytes);
    }
}

// ================
// PUBLIC FUNCTIONS
// ================

int message_processor_init(ring_buffer_t *rx_queue,
                          struct messageHistory *message_history,
                          struct nodeHistory *node_list)
{
    if (!rx_queue || !message_history || !node_list) {
        return -EINVAL;
    }
    
    g_rx_queue = rx_queue;
    g_message_history = message_history;
    g_node_list = node_list;
    memset(&g_stats, 0, sizeof(g_stats));
    
    return 0;
}

static void message_processor_thread_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    
    meshtastic_FromRadio msg;
    
    printk("Message processor thread started\n");
    
    while (1) {
        // Wait for a message to be available in the ring buffer
        if (ring_buffer_wait(g_rx_queue, K_FOREVER)) {
            // Process all available messages
            while (ring_buffer_get(g_rx_queue, &msg)) {
                process_message(&msg);
            }
        }
    }
}

int message_processor_start(void)
{
    if (!g_rx_queue || !g_message_history || !g_node_list) {
        printk("Message processor not initialized\n");
        return -EINVAL;
    }
    
    // Create the thread
    k_thread_create(&g_msg_task_thread, g_msg_task_stack,
                    K_THREAD_STACK_SIZEOF(g_msg_task_stack),
                    (k_thread_entry_t)message_processor_thread_entry,
                    NULL, NULL, NULL,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    
    printk("Message processor thread created\n");
    return 0;
}

bool message_processor_wait_for_my_node_info(int timeout_ms)
{
    if (!g_node_list) return false;
    
    int elapsed_ms = 0;
    
    printk("Waiting for my node info...\n");
    
    while (!g_node_list->my_info.valid && elapsed_ms < timeout_ms) {
        send_want_config();
        k_sleep(K_SECONDS(2));
        elapsed_ms += 2000;
    }
    
    if (g_node_list->my_info.valid) {
        printk("My node info received! Node num: %u\n", (unsigned int)g_node_list->my_info.num);
        return true;
    } else {
        printk("Timeout waiting for my node info\n");
        return false;
    }
}

void message_processor_get_stats(uint32_t *processed_count, uint32_t *error_count)
{
    if (processed_count) *processed_count = g_stats.messages_processed;
    if (error_count) *error_count = g_stats.errors;
}