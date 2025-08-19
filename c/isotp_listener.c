#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define UDS_BUFFER_SIZE 4095

// eval_msg return codes
#define MSG_NO_UDS 0
#define MSG_UDS_OK 1
#define MSG_UDS_WRONG_FORMAT -1
#define MSG_UDS_UNEXPECTED_CF -2
#define MSG_UDS_ERROR -3

// Enums to represent Python classes for constants
typedef enum {
    Sleeping = 0,
    First = 1,
    Consecutive = 2,
    WaitConsecutive = 3,
    FlowControl = 4
} ActualState;

typedef enum {
    Single = 0,
    First_Frame = 1,
    Consecutive_Frame = 2,
    FlowControl_Frame = 3
} FrameType;

typedef enum {
    Service = 0,
    FlowControl = 1
} RequestType;

// Function pointer types to mimic Python's callback functions
typedef void (*send_frame_func)(uint32_t can_id, uint8_t *data, size_t nr_of_bytes);
typedef size_t (*uds_handler_func)(RequestType type, uint8_t *rx_data, size_t rx_size, uint8_t *tx_buffer);

// Struct to represent the IsoTpOptions class
typedef struct {
    uint32_t source_address;
    uint32_t target_address;
    int bs;
    int stmin;
    int frame_timeout;
    int wftmax;
    send_frame_func send_frame;
    uds_handler_func uds_handler;
} IsoTpOptions;

// Struct to represent the Isotp_Listener class
typedef struct {
    IsoTpOptions options;
    int last_action_tick;
    int this_tick;
    ActualState actual_state;
    uint8_t receive_buffer[UDS_BUFFER_SIZE];
    uint8_t send_buffer[UDS_BUFFER_SIZE];
    uint8_t telegrambuffer[8];
    size_t actual_telegram_pos;
    size_t actual_send_pos;
    size_t actual_send_buffer_size;
    size_t actual_receive_pos;
    size_t expected_receive_buffer_size;
    uint8_t actual_cf_count;
    uint8_t receive_cf_count;
    int flow_control_block_size;
    int receive_flow_control_block_count;
    int consecutive_frame_delay;
    int last_frame_received_tick;
} Isotp_Listener;

// Function prototypes
void Isotp_Listener_init(Isotp_Listener *self, IsoTpOptions options);
void update_options(Isotp_Listener *self, IsoTpOptions options);
IsoTpOptions get_options(Isotp_Listener *self);
int tick(Isotp_Listener *self, int time_ticks);
size_t copy_to_telegram_buffer(Isotp_Listener *self);
size_t read_from_can_msg(Isotp_Listener *self, uint8_t *data, size_t start, size_t nr_of_bytes);
void send_cf_telegram(Isotp_Listener *self);
void send_telegram(Isotp_Listener *self, uint8_t *data, size_t nr_of_bytes);
void buffer_tx(Isotp_Listener *self);
void handle_received_message(Isotp_Listener *self, size_t nr_of_bytes);
int eval_msg(Isotp_Listener *self, uint32_t can_id, uint8_t *data, size_t nr_of_bytes);
int busy(Isotp_Listener *self);

// Isotp_Listener constructor
void Isotp_Listener_init(Isotp_Listener *self, IsoTpOptions options) {
    self->options = options;
    self->last_action_tick = 0;
    self->this_tick = 0;
    self->actual_state = Sleeping;
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
size_t copy_to_telegram_buffer(Isotp_Listener *self) {
    size_t nr_of_bytes = 0;
    while (self->actual_telegram_pos < 8 && self->actual_send_pos < self->actual_send_buffer_size) {
        self->telegrambuffer[self->actual_telegram_pos] = self->send_buffer[self->actual_send_pos];
        nr_of_bytes++;
        self->actual_telegram_pos++;
        self->actual_send_pos++;
    }
    for (size_t i = self->actual_telegram_pos; i < 8; i++) {
        self->telegrambuffer[i] = 0; // fill padding bytes
    }
    return nr_of_bytes;
}

// read data from the can message into the receive buffer and set all data accordingly
size_t read_from_can_msg(Isotp_Listener *self, uint8_t *data, size_t start, size_t nr_of_bytes) {
    size_t bytes_read = 0;
    while (nr_of_bytes > 0 && self->actual_receive_pos < UDS_BUFFER_SIZE && start < 8) {
        self->receive_buffer[self->actual_receive_pos] = data[start];
        bytes_read++;
        start++;
        self->actual_receive_pos++;
        nr_of_bytes--;
    }
    return bytes_read;
}

// sent next consecutive frame and set all data accordingly
void send_cf_telegram(Isotp_Listener *self) {
    self->telegrambuffer[0] = 0x20 | self->actual_cf_count;
    self->actual_cf_count = (self->actual_cf_count + 1) & 0x0F;
    size_t nr_of_bytes = 1;
    self->actual_telegram_pos = 1;
    nr_of_bytes += copy_to_telegram_buffer(self);
    if (self->options.send_frame) {
        self->options.send_frame(self->options.target_address, self->telegrambuffer, 8);
    }
    self->last_action_tick = self->this_tick;
    if (self->actual_send_pos >= self->actual_send_buffer_size) {
        printf("%zu Bytes sent\n", self->actual_send_buffer_size);
        self->actual_state = Sleeping;
        return;
    }
    if (self->flow_control_block_size > -1) {
        self->flow_control_block_size--;
        if (self->flow_control_block_size < 1) {
            self->actual_state = FlowControl;
        }
    }
}

// Transmit telegram
void send_telegram(Isotp_Listener *self, uint8_t *data, size_t nr_of_bytes) {
    if (nr_of_bytes > UDS_BUFFER_SIZE) {
        printf("ERROR: data size too big with %zu Bytes\n", nr_of_bytes);
        return;
    }
    memcpy(self->send_buffer, data, nr_of_bytes);
    self->actual_send_buffer_size = nr_of_bytes;
    buffer_tx(self);
}

void buffer_tx(Isotp_Listener *self) {
    if (!self->actual_send_buffer_size) {
        return;
    }
    if (self->actual_send_buffer_size < 8) { // fits into a single frame
        self->telegrambuffer[0] = self->actual_send_buffer_size;
        self->actual_telegram_pos = 1;
        self->actual_send_pos = 0;
        size_t nr_of_bytes = 1 + copy_to_telegram_buffer(self);
        if (self->options.send_frame) {
            self->options.send_frame(self->options.target_address, self->telegrambuffer, nr_of_bytes);
        }
    } else { // generate first frame
        self->telegrambuffer[0] = 0x10 | (self->actual_send_buffer_size >> 8);
        self->telegrambuffer[1] = self->actual_send_buffer_size & 0xFF;
        self->actual_telegram_pos = 2;
        self->actual_send_pos = 0;
        size_t nr_of_bytes = 2 + copy_to_telegram_buffer(self);
        if (self->options.send_frame) {
            self->options.send_frame(self->options.target_address, self->telegrambuffer, nr_of_bytes);
        }
        self->actual_state = FlowControl;
    }
}

void handle_received_message(Isotp_Listener *self, size_t nr_of_bytes) {
    printf("%zu Bytes received\n", nr_of_bytes);
    self->actual_state = Sleeping;
    if (self->options.uds_handler) {
        self->actual_send_buffer_size = self->options.uds_handler(Service, self->receive_buffer, nr_of_bytes, self->send_buffer);
        printf("Answer with %zu Bytes\n", self->actual_send_buffer_size);
        buffer_tx(self);
    }
}

/*
 checks, if the given can message is a isotp message.
 returns MSG_xx error codes
 */
int eval_msg(Isotp_Listener *self, uint32_t can_id, uint8_t *data, size_t nr_of_bytes) {
    if (can_id != self->options.source_address) {
        return MSG_NO_UDS;
    }
    if (nr_of_bytes < 1) {
        return MSG_UDS_WRONG_FORMAT;
    }
    int frame_identifier = data[0] >> 4;
    size_t dl = data[0] & 0x0F;

    if (frame_identifier > 3) {
        return MSG_UDS_WRONG_FORMAT;
    }
    int frametype = frame_identifier;
    self->last_frame_received_tick = self->this_tick;

    if (frametype == First_Frame) {
        printf("First Frame\n");
        dl = (data[0] & 0x0F) * 256 + data[1];
        self->actual_receive_pos = 0;
        self->receive_cf_count = 1;
        self->expected_receive_buffer_size = dl;
        if (dl > 6) {
            dl = 6;
        }
        read_from_can_msg(self, data, 2, dl);

        if (self->options.send_frame) {
            self->telegrambuffer[0] = 0x30;
            self->telegrambuffer[1] = self->options.bs;
            self->telegrambuffer[2] = self->options.stmin;
            self->options.send_frame(self->options.target_address, self->telegrambuffer, 3);
        }
        self->receive_flow_control_block_count = self->options.bs;
        if (self->receive_flow_control_block_count == 0) {
            self->receive_flow_control_block_count = -1;
        }
        self->actual_state = WaitConsecutive;
        return MSG_UDS_OK;
    }

    if (frametype == FlowControl_Frame) {
        int flow_status = data[0] & 0x0F;
        printf("Flow Control\n");
        if (flow_status == 1) { // Wait
            return MSG_UDS_OK;
        }
        if (flow_status == 2) { // Overflow
            self->actual_state = Sleeping;
            return MSG_UDS_OK;
        }
        if (flow_status == 3) { // Undefined
            self->actual_state = Sleeping;
            return MSG_UDS_WRONG_FORMAT;
        }
        // Clear to send (flow_status == 0)
        self->flow_control_block_size = data[1];
        if (self->flow_control_block_size == 0) {
            self->flow_control_block_size = -1;
        }
        self->consecutive_frame_delay = data[2];
        self->actual_cf_count = 1;
        self->actual_state = Consecutive;
        return MSG_UDS_OK;
    }

    if (frametype == Single) {
        printf("Single Frame\n");
        self->actual_receive_pos = 0;
        if (read_from_can_msg(self, data, 1, dl)) {
            handle_received_message(self, dl);
        }
        self->actual_state = Sleeping;
        return MSG_UDS_OK;
    }

    if (frametype == Consecutive_Frame) {
        if (self->actual_state == WaitConsecutive) {
            if (self->receive_cf_count != (data[0] & 0x0F)) {
                printf("wrong CF sequence number\n");
                if (self->options.send_frame) {
                    self->telegrambuffer[0] = 0x32;
                    self->telegrambuffer[1] = 0;
                    self->telegrambuffer[2] = 0;
                    self->options.send_frame(self->options.target_address, self->telegrambuffer, 3);
                }
                return MSG_UDS_UNEXPECTED_CF;
            }
            self->receive_cf_count = (self->receive_cf_count + 1) & 0x0F;
            if (read_from_can_msg(self, data, 1, self->expected_receive_buffer_size - self->actual_receive_pos)) {
                if (self->actual_receive_pos == self->expected_receive_buffer_size) {
                    self->actual_state = Sleeping;
                    handle_received_message(self, self->expected_receive_buffer_size);
                    return MSG_UDS_OK;
                }
                if (self->receive_flow_control_block_count > -1) {
                    if (self->receive_flow_control_block_count > 0) {
                        self->receive_flow_control_block_count--;
                    }
                    if (self->receive_flow_control_block_count == 0) {
                        if (self->options.send_frame) {
                            self->telegrambuffer[0] = 0x30;
                            self->telegrambuffer[1] = self->options.bs;
                            self->telegrambuffer[2] = self->options.stmin;
                            self->options.send_frame(self->options.target_address, self->telegrambuffer, 3);
                        }
                        self->receive_flow_control_block_count = self->options.bs;
                        if (self->receive_flow_control_block_count == 0) {
                            self->receive_flow_control_block_count = -1;
                        }
                    }
                }
                return MSG_UDS_OK;
            } else {
                self->actual_state = Sleeping;
                return MSG_UDS_WRONG_FORMAT;
            }
        } else {
            printf("unexpected CF\n");
            if (self->options.send_frame) {
                self->telegrambuffer[0] = 0x32;
                self->telegrambuffer[1] = 0;
                self->telegrambuffer[2] = 0;
                self->options.send_frame(self->options.target_address, self->telegrambuffer, 3);
            }
            return MSG_UDS_UNEXPECTED_CF;
        }
    }
    return MSG_UDS_ERROR;
}

// True if a transfer is actual ongoing
int busy(Isotp_Listener *self) {
    return self->actual_state != Sleeping;
}

// Tick function
int tick(Isotp_Listener *self, int time_ticks) {
    self->this_tick = time_ticks;
    if (self->actual_state == Consecutive) {
        if (self->last_action_tick + self->consecutive_frame_delay < self->this_tick) {
            send_cf_telegram(self);
        }
        return 0; // False
    }
    if (self->actual_state == FlowControl || self->actual_state == WaitConsecutive) {
        if (self->last_frame_received_tick + self->options.frame_timeout < self->this_tick) {
            printf("Tick Timeout\n");
            self->actual_state = Sleeping;
            return 1; // True
        }
        return 0; // False
    }
    return 0; // False
}