#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// DEBUG output - (un)comment as needed
#define DEBUG(x) \
    printf(x)
// #define DEBUG(x)

#define UDS_BUFFER_SIZE 4095

typedef unsigned char uds_buffer[UDS_BUFFER_SIZE];


typedef struct   
{
    unsigned char const Service ;
    unsigned char const FlowControl;
} RequestType;



typedef struct 
{
    unsigned char const Single;
    unsigned char const First;
    unsigned char const Consecutive;
    unsigned char const FlowControl ;
} FrameType;


// a (growing) list of UDS services
typedef struct 
{
    unsigned char const ClearDTCs ;
    unsigned char const ReadDTC ;
} Service;


typedef struct 
{
    unsigned char const First;
    unsigned char const Sleeping ;
    unsigned char const Consecutive ;
    unsigned char const WaitConsecutive;
    unsigned char const FlowControl ;
} ActualState;


// eval_msg return codes
#define MSG_NO_UDS 0             // no uds message, should be proceded by the application
#define MSG_UDS_OK 1             // successfully handled by isotp_listener
#define MSG_UDS_WRONG_FORMAT -1  // message format out of spec
#define MSG_UDS_UNEXPECTED_CF -2 // not wating for a CF
#define MSG_UDS_ERROR -3         // unclear error


// structure to initialize the isotp_listener constructor
struct sIsoTpOptions
{
    uint32_t source_address;      // The source address of the UDS service. This is the address of the ECU that is listening to the CAN bus
    uint32_t target_address;      // The target address of the UDS service. This is the address of the ECU that is sending the CAN messages
    int bs;                  // The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
    int stmin;               // The minimum separation time sent in the flow control message. Indicates the amount of time to wait between 2 consecutive frame. This value will be sent as is over CAN. Values from 1 to 127 means milliseconds. Values from 0xF1 to 0xF9 means 100us to 900us. 0 Means no timing requirements
    int wftmax;              // Maximum number of wait frame (flow control message with flow status=1) allowed before dropping a message. 0 means that wait frame are not allowed
    int frame_timeout ; // maximal allowed time in ms between two received frames to keep the transfer active
    void (*send_frame)(uint32_t can_id, uint8_t *data, size_t nr_of_bytes); // Function to send a CAN message. This function will be called by the isotp_listener to send a CAN message
    size_t (*uds_handler)(unsigned char type, uint8_t *rx_data, size_t rx_size, uint8_t *tx_buffer); // Function to handle UDS messages. This function will be called by the isotp_listener when a complete UDS message has been received
    //void *send_frame; // Function to send a CAN message. This function will be called by the isotp_listener to send a CAN message
    //int *uds_handler; // Function to handle UDS messages. This function will be called by the isotp_listener when a complete UDS message has been received
} ;


typedef struct sIsoTpOptions IsoTpOptions;


// the Isotp_Listener class
typedef struct Isotp_Listener
{
    IsoTpOptions * options;
    uint64_t last_action_tick;
    uint64_t last_frame_received_tick ;
    uint64_t this_tick ;
    unsigned char actual_state;
    uds_buffer receive_buffer;
    uds_buffer send_buffer;
    uint8_t telegrambuffer[8];
    int actual_telegram_pos;
    int actual_send_pos;
    int actual_send_buffer_size;
    int actual_receive_pos;
    int expected_receive_buffer_size;
    int actual_cf_count;
    int receive_cf_count;
    int flow_control_block_size;
    int receive_flow_control_block_count;
    int consecutive_frame_delay;
} Isotp_Listener;


// Function prototypes
void Isotp_Listener_init(struct Isotp_Listener *self, IsoTpOptions * options);
IsoTpOptions get_options(struct Isotp_Listener *self);
int tick(struct Isotp_Listener *self, int time_ticks);
int copy_to_telegram_buffer(struct Isotp_Listener *self);
int read_from_can_msg(struct Isotp_Listener *self, uint8_t *data, int start, int nr_of_bytes);
void send_cf_telegram(struct Isotp_Listener *self);
void send_telegram(struct Isotp_Listener *self, uint8_t *data, int nr_of_bytes);
void buffer_tx(struct Isotp_Listener *self);
void handle_received_message(struct Isotp_Listener *self, int nr_of_bytes);
int eval_msg(struct Isotp_Listener *self, uint32_t can_id, uint8_t *data, int nr_of_bytes);
int busy(struct Isotp_Listener *self);
#endif // ISOTP_LISTENER_H
// End of c/isotp_listener.h