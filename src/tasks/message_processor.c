#include "tasks/message_processor.h"
#include "models/mesh_message.h"
#include "models/mesh_node.h"
#include "models/ring_buffer.h"
#include "comms/protobuf_handler.h"
#include "ui/display_ui.h"
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
static struct display_ui_t *g_display_ui = NULL;

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

// Takes a message from the ring buffer and updates the message history and node history as appropriate.
static void process_message(const meshtastic_FromRadio *msg)
{
    if (!msg) return;
    
    // Handle queue status messages
    if (msg->which_payload_variant == meshtastic_FromRadio_queueStatus_tag) {
        return;
    }
    
    // Handle node info updates
    if (msg->which_payload_variant == meshtastic_FromRadio_my_info_tag ||
        msg->which_payload_variant == meshtastic_FromRadio_node_info_tag) {
        update_node_history(g_node_list, msg);
        
        return;
    }
    
    // Handle text messages
    if (msg->which_payload_variant == meshtastic_FromRadio_packet_tag &&
        msg->packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        msg->packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        
        // Ignore messages from myself (they're already added to history)
        if (g_node_list->my_info.valid && 
            msg->packet.from == g_node_list->my_info.num) {
            return;
        }
        
        // Update message history
        update_message_history(g_message_history, msg);
        g_stats.messages_processed++;
        
        // Get the parsed message
        struct message parsed_msg = parse_message(msg);
        
        // Notify the UI about the new message
        if (g_display_ui != NULL && g_display_ui->initialized) {
            display_ui_notify_new_message((display_ui_t *)g_display_ui, &parsed_msg);
        }
    }
}

// ================
// PUBLIC FUNCTIONS
// ================

// Stores references to the ring buffer, message history, and node list for use by the message processor thread.
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
    g_display_ui = NULL;
    memset(&g_stats, 0, sizeof(g_stats));
    
    return 0;
}

// Sets the display UI reference for the message processor to notify about new messages.
void message_processor_set_display_ui(struct display_ui_t *ui)
{
    g_display_ui = ui;
    if (ui != NULL) {
        printk("Display UI set for message notifications\n");
    }
}

static void message_processor_thread_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    
    meshtastic_FromRadio msg;
    
    printk("Message processor thread started\n");
    
    while (1) {
        if (ring_buffer_wait(g_rx_queue, K_FOREVER)) {
            while (ring_buffer_get(g_rx_queue, &msg)) {
                process_message(&msg);
            }
        }
    }
}

// Starts the message processing thread with appropriate priority and stack size.
int message_processor_start(void)
{
    if (!g_rx_queue || !g_message_history || !g_node_list) {
        printk("Message processor not initialized\n");
        return -EINVAL;
    }
    
    k_thread_create(&g_msg_task_thread, g_msg_task_stack,
                    K_THREAD_STACK_SIZEOF(g_msg_task_stack),
                    (k_thread_entry_t)message_processor_thread_entry,
                    NULL, NULL, NULL,
                    K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    
    printk("Message processor thread created\n");
    return 0;
}

// Wait for my node info to be received, which indicates we've joined the mesh and can start processing messages.
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

// Retrieves the current message processing statistics.
void message_processor_get_stats(uint32_t *processed_count, uint32_t *error_count)
{
    if (processed_count) *processed_count = g_stats.messages_processed;
    if (error_count) *error_count = g_stats.errors;
}