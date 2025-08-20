#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "isotp_listener.h"

RequestType requestType = {
    .Service = 0x00,
    .FlowControl = 0x01
};
ActualState actualState = {
    .First = 0,
    .Sleeping = 1,
    .Consecutive = 2,
    .WaitConsecutive = 3,
    .FlowControl = 4
};
FrameType frameType = {
    .Single = 0,
    .First = 1,
    .Consecutive = 2,
    .FlowControl = 3
};
Service service = {
    .ClearDTCs = 0x14,
    .ReadDTC = 0x19
};


// Isotp_Listener constructor
void Isotp_Listener_init( Isotp_Listener *self,  IsoTpOptions * options) {
    self->options = options;
    self->last_action_tick = 0;
    self->this_tick = 0;
    self->actual_state = actualState.Sleeping;
    memset(self->receive_buffer, 0, UDS_BUFFER_SIZE);
    memset(self->send_buffer, 0, UDS_BUFFER_SIZE);
    memset(self->telegrambuffer, 0, 8);
    self->actual_telegram_pos = 0;
    self->actual_send_pos = 0;
    self->actual_send_buffer_size = 0;
    self->actual_receive_pos = 0;
    self->expected_receive_buffer_size = 0;
    self->actual_cf_count = 0;
    self->receive_cf_count = 0;
    self->flow_control_block_size = 0;
    self->receive_flow_control_block_count = 0;
    self->consecutive_frame_delay = 0;
    self->last_frame_received_tick = 0;
}

// transfers data from the send buffer into the can message and set all data accordingly
int copy_to_telegram_buffer(struct Isotp_Listener *self) {
    int nr_of_bytes = 0;
    while (self->actual_telegram_pos < 8 && self->actual_send_pos < self->actual_send_buffer_size) {
        self->telegrambuffer[self->actual_telegram_pos] = self->send_buffer[self->actual_send_pos];
        nr_of_bytes++;
        self->actual_telegram_pos++;
        self->actual_send_pos++;
    }
    for (int i = self->actual_telegram_pos; i < 8; i++) {
        self->telegrambuffer[i] = 0; // fill padding bytes
    }
    return nr_of_bytes;
}

// read data from the can message into the receive buffer and set all data accordingly
int read_from_can_msg(struct Isotp_Listener *self, uint8_t *data, int start, int nr_of_bytes) {
    int bytes_read = 0;
    while (nr_of_bytes > 0 && self->actual_receive_pos < UDS_BUFFER_SIZE && start < 8) {
        self->receive_buffer[self->actual_receive_pos] = data[start];
        bytes_read++;
        start++;
        self->actual_receive_pos++;
        nr_of_bytes--;
    }
    return bytes_read;
}

// sent next actualState.Consecutive frame and set all data accordingly
void send_cf_telegram(struct Isotp_Listener *self) {
    self->telegrambuffer[0] = 0x20 | self->actual_cf_count;
    self->actual_cf_count = (self->actual_cf_count + 1) & 0x0F;
    int nr_of_bytes = 1;
    self->actual_telegram_pos = 1;
    nr_of_bytes += copy_to_telegram_buffer(self);
    if (self->options->send_frame) {
        self->options->send_frame(self->options->target_address, self->telegrambuffer, 8);
    }
    self->last_action_tick = self->this_tick;
    if (self->actual_send_pos >= self->actual_send_buffer_size) {
        printf("%d Bytes sent\n", self->actual_send_buffer_size);
        self->actual_state = actualState.Sleeping;
        return;
    }
    if (self->flow_control_block_size > -1) {
        self->flow_control_block_size--;
        if (self->flow_control_block_size < 1) {
            self->actual_state = actualState.FlowControl;
        }
    }
}

// Transmit telegram
void send_telegram(struct Isotp_Listener *self, uint8_t *data, int nr_of_bytes) {
    if (nr_of_bytes > UDS_BUFFER_SIZE) {
        printf("ERROR: data size too big with %d Bytes\n", nr_of_bytes);
        return;
    }
    memcpy(self->send_buffer, data, nr_of_bytes);
    self->actual_send_buffer_size = nr_of_bytes;
    buffer_tx(self);
}

void buffer_tx(struct Isotp_Listener *self) {
    if (!self->actual_send_buffer_size) {
        return;
    }
    if (self->actual_send_buffer_size < 8) { // fits into a single frame
        self->telegrambuffer[0] = self->actual_send_buffer_size;
        self->actual_telegram_pos = 1;
        self->actual_send_pos = 0;
        int nr_of_bytes = 1 + copy_to_telegram_buffer(self);
        if (self->options->send_frame) {
            self->options->send_frame(self->options->target_address, self->telegrambuffer, nr_of_bytes);
        }
    } else { // generate first frame
        self->telegrambuffer[0] = 0x10 | (self->actual_send_buffer_size >> 8);
        self->telegrambuffer[1] = self->actual_send_buffer_size & 0xFF;
        self->actual_telegram_pos = 2;
        self->actual_send_pos = 0;
        int nr_of_bytes = 2 + copy_to_telegram_buffer(self);
        if (self->options->send_frame) {
            self->options->send_frame(self->options->target_address, self->telegrambuffer, nr_of_bytes);
        }
        self->actual_state = actualState.FlowControl;
    }
}

void handle_received_message(struct Isotp_Listener *self, int nr_of_bytes) {
    printf("%d Bytes received\n", nr_of_bytes);
    self->actual_state = actualState.Sleeping;
    if (self->options->uds_handler) {
        self->actual_send_buffer_size = self->options->uds_handler(requestType.Service, self->receive_buffer, nr_of_bytes, self->send_buffer);
        printf("Answer with %d Bytes\n", self->actual_send_buffer_size);
        buffer_tx(self);
    }
}

/*
 checks, if the given can message is a isotp message.
 returns MSG_xx error codes
 */
int eval_msg(struct Isotp_Listener *self, uint32_t can_id, uint8_t *data, int nr_of_bytes) {
    if (can_id != self->options->source_address) {
        return MSG_NO_UDS;
    }
    if (nr_of_bytes < 1) {
        return MSG_UDS_WRONG_FORMAT;
    }
    int frame_identifier = data[0] >> 4;
    int dl = data[0] & 0x0F;

    if (frame_identifier > 3) {
        return MSG_UDS_WRONG_FORMAT;
    }
    int frametype = frame_identifier;
    self->last_frame_received_tick = self->this_tick;

    if (frametype == frameType.First) {
        printf("First Frame\n");
        dl = (data[0] & 0x0F) * 256 + data[1];
        self->actual_receive_pos = 0;
        self->receive_cf_count = 1;
        self->expected_receive_buffer_size = dl;
        if (dl > 6) {
            dl = 6;
        }
        read_from_can_msg(self, data, 2, dl);

        if (self->options->send_frame) {
            self->telegrambuffer[0] = 0x30;
            self->telegrambuffer[1] = self->options->bs;
            self->telegrambuffer[2] = self->options->stmin;
            self->options->send_frame(self->options->target_address, self->telegrambuffer, 3);
        }
        self->receive_flow_control_block_count = self->options->bs;
        if (self->receive_flow_control_block_count == 0) {
            self->receive_flow_control_block_count = -1;
        }
        self->actual_state = actualState.WaitConsecutive;
        return MSG_UDS_OK;
    }

    if (frametype == actualState.FlowControl) {
        int flow_status = data[0] & 0x0F;
        printf("Flow Control\n");
        if (flow_status == 1) { // Wait
            return MSG_UDS_OK;
        }
        if (flow_status == 2) { // Overflow
            self->actual_state = actualState.Sleeping;
            return MSG_UDS_OK;
        }
        if (flow_status == 3) { // Undefined
            self->actual_state = actualState.Sleeping;
            return MSG_UDS_WRONG_FORMAT;
        }
        // Clear to send (flow_status == 0)
        self->flow_control_block_size = data[1];
        if (self->flow_control_block_size == 0) {
            self->flow_control_block_size = -1;
        }
        self->consecutive_frame_delay = data[2];
        self->actual_cf_count = 1;
        self->actual_state = actualState.Consecutive;
        return MSG_UDS_OK;
    }

    if (frametype == frameType.Single) {
        printf("Single Frame\n");
        self->actual_receive_pos = 0;
        if (read_from_can_msg(self, data, 1, dl)) {
            handle_received_message(self, dl);
        }
        self->actual_state = actualState.Sleeping;
        return MSG_UDS_OK;
    }

    if (frametype == actualState.Consecutive) {
        if (self->actual_state == actualState.WaitConsecutive) {
            if (self->receive_cf_count != (data[0] & 0x0F)) {
                printf("wrong CF sequence number\n");
                if (self->options->send_frame) {
                    self->telegrambuffer[0] = 0x32;
                    self->telegrambuffer[1] = 0;
                    self->telegrambuffer[2] = 0;
                    self->options->send_frame(self->options->target_address, self->telegrambuffer, 3);
                }
                return MSG_UDS_UNEXPECTED_CF;
            }
            self->receive_cf_count = (self->receive_cf_count + 1) & 0x0F;
            if (read_from_can_msg(self, data, 1, self->expected_receive_buffer_size - self->actual_receive_pos)) {
                if (self->actual_receive_pos == self->expected_receive_buffer_size) {
                    self->actual_state = actualState.Sleeping;
                    handle_received_message(self, self->expected_receive_buffer_size);
                    return MSG_UDS_OK;
                }
                if (self->receive_flow_control_block_count > -1) {
                    if (self->receive_flow_control_block_count > 0) {
                        self->receive_flow_control_block_count--;
                    }
                    if (self->receive_flow_control_block_count == 0) {
                        if (self->options->send_frame) {
                            self->telegrambuffer[0] = 0x30;
                            self->telegrambuffer[1] = self->options->bs;
                            self->telegrambuffer[2] = self->options->stmin;
                            self->options->send_frame(self->options->target_address, self->telegrambuffer, 3);
                        }
                        self->receive_flow_control_block_count = self->options->bs;
                        if (self->receive_flow_control_block_count == 0) {
                            self->receive_flow_control_block_count = -1;
                        }
                    }
                }
                return MSG_UDS_OK;
            } else {
                self->actual_state = actualState.Sleeping;
                return MSG_UDS_WRONG_FORMAT;
            }
        } else {
            printf("unexpected CF\n");
            if (self->options->send_frame) {
                self->telegrambuffer[0] = 0x32;
                self->telegrambuffer[1] = 0;
                self->telegrambuffer[2] = 0;
                self->options->send_frame(self->options->target_address, self->telegrambuffer, 3);
            }
            return MSG_UDS_UNEXPECTED_CF;
        }
    }
    return MSG_UDS_ERROR;
}

// True if a transfer is actual ongoing
int busy(struct Isotp_Listener *self) {
    return self->actual_state != actualState.Sleeping;
}

// Tick function
int tick(struct Isotp_Listener *self, int time_ticks) {
    self->this_tick = time_ticks;
    if (self->actual_state == actualState.Consecutive) {
        if (self->last_action_tick + self->consecutive_frame_delay < self->this_tick) {
            send_cf_telegram(self);
        }
        return 0; // False
    }
    if (self->actual_state == actualState.FlowControl || self->actual_state == actualState.WaitConsecutive) {
        if (self->last_frame_received_tick + self->options->frame_timeout < self->this_tick) {
            printf("Tick Timeout\n");
            self->actual_state = actualState.Sleeping;
            return 1; // True
        }
        return 0; // False
    }
    return 0; // False
}