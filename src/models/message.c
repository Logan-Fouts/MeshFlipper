#include <zephyr/kernel.h>
#include "models/message.h"


struct message parse_message(const meshtastic_FromRadio *msg_packet)
{
    // TODO: Implement actual parsing logic to populate the message structure based on the incoming packet
    struct message msg = {
        .id = 0,
        .from = 0,
        .to = 0,
        .text = {0},
    };

    if (msg_packet != NULL) {
        msg.id = msg_packet->packet.id;
        msg.from = msg_packet->packet.from;
        msg.to = msg_packet->packet.to;

        size_t copy_len = msg_packet->packet.decoded.payload.size;

        // This should never happen since the Meshtastic radio should enforce the max message length, but we check just in case to avoid buffer overflows.
        if (copy_len >= sizeof(msg.text)) {
            copy_len = sizeof(msg.text) - 1;
        }

        memcpy(msg.text, msg_packet->packet.decoded.payload.bytes, copy_len);
        msg.text[copy_len] = '\0';
    }

    return msg;
}

void print_message(struct message *msg)
{
    if (msg == NULL) {
        printk("\n[Message]\n  <null>\n");
        return;
    }

    printk("\n[Message]\n");
    printk("  id:            %d\n", msg->id);
    printk("  sender:        %d\n", msg->from);
    printk("  destination:   %d\n", msg->to);
    printk("  text:          %s\n", msg->text);
}

void update_message_history(struct messageHistory *mes_history, const meshtastic_FromRadio *msg_packet)
{
    if (mes_history == NULL || msg_packet == NULL) {
        return;
    }

    struct message parsed_message = parse_message(msg_packet);

    if (parsed_message.id == 0) {
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&mes_history->lock);

    // Check for duplicates before adding to history
    if (find_message_by_id(mes_history, parsed_message.id) != NULL) {
        k_spin_unlock(&mes_history->lock, key);
        return;
    }

    mes_history->messages[mes_history->count % MAX_MESSAGE_HISTORY] = parsed_message;
    mes_history->count++;


    k_spin_unlock(&mes_history->lock, key);
}


// Returns a dynamically allocated array of pointers to messages sent to the specified node, and sets out_count to the number of messages found.
// The caller is responsible for freeing the returned array.
struct message** find_my_messages_sent_to_node(struct messageHistory *mes_history, int node_num, size_t *out_count)
{
    if (mes_history == NULL || mes_history->count == 0 || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }

    struct message **results = k_malloc(sizeof(struct message *) * mes_history->count);
    size_t results_count = 0;

    for (int i = 0; i < mes_history->count && results_count < mes_history->count; i++) {
        struct message *msg_ptr;

        //Breifly aquire lock before accessing specific mesage from history
        k_spinlock_key_t key = k_spin_lock(&mes_history->lock);
        msg_ptr = &mes_history->messages[i];
        k_spin_unlock(&mes_history->lock, key);

        if (msg_ptr->to == node_num) {
            results[results_count++] = msg_ptr;
        }
    }

    *out_count = results_count;
    return results;
}

void print_message_history(struct messageHistory *mes_history)
{
    if (mes_history->count == 0) {
        return;
    }

    for (int i = 0; i < mes_history->count; i++) {
        struct message msg_copy;

        //Breifly aquire lock before accessing specific mesage from history
        k_spinlock_key_t key = k_spin_lock(&mes_history->lock);
        msg_copy = mes_history->messages[i];
        k_spin_unlock(&mes_history->lock, key);

        print_message(&msg_copy);
    }
}



struct message* find_message_by_id(struct messageHistory *mes_history, int id)
{
    if (mes_history == NULL || mes_history->count == 0) {
        return NULL;
    }

    for (int i = 0; i < mes_history->count; i++) {
        struct message *msg_ptr;

        //Breifly aquire lock before accessing specific mesage from history
        k_spinlock_key_t key = k_spin_lock(&mes_history->lock);
        msg_ptr = &mes_history->messages[i];
        k_spin_unlock(&mes_history->lock, key);

        if (msg_ptr->id == id) {
            return msg_ptr;
        }
    }

    return NULL;
}
