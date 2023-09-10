#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

#include <cstdint>

// DEBUG output - (un)comment as needed
#define DEBUG(x) do { std::cerr << x; } while (0)
//#define DEBUG(x)

#define UDS_BUFFER_SIZE 4095
typedef unsigned char uds_buffer[UDS_BUFFER_SIZE];

enum class RequestType { Service, FlowControl };
enum class FrameType { Single, First, Consecutive, FlowControl };
enum class ActualState { Sleeping, First, Consecutive, WaitConsecutive, FlowControl };


// eval_msg return codes
#define MSG_NO_UDS 0 // no uds message, should be proceded by the application
#define MSG_UDS_OK 1 // successfully handled by isotp_listener
#define MSG_UDS_WRONG_FORMAT -1 // message format out of spec
#define MSG_UDS_UNEXPECTED_CF -2 // not wating for a CF
#define MSG_UDS_ERROR -3 // unclear error


// structure to initialize the isotp_listener constructor
struct isotp_options
{
    int source_address=0;
    int target_address=0;
    int bs=0 ; // The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
    int stmin =0 ; // The minimum separation time sent in the flow control message. Indicates the amount of time to wait between 2 consecutive frame. This value will be sent as is over CAN. Values from 1 to 127 means milliseconds. Values from 0xF1 to 0xF9 means 100us to 900us. 0 Means no timing requirements
    int wftmax = 0 ;// Maximum number of wait frame (flow control message with flow status=1) allowed before dropping a message. 0 means that wait frame are not allowed
    int(*send_telegram)(int,unsigned char[8],int len)=0;
    bool(*uds_handler)(RequestType request_type , uds_buffer  receive_buffer, int recv_len,  uds_buffer  send_buffer, int & send_len)=0;


};


// a (growing) list of UDS services
class Service
{
    public:
    static unsigned char const ClearDTCs= 0x14;
    static unsigned char const ReadDTC= 0x19;
} ;


// the Isotp_Listener class
class Isotp_Listener
{
private:
    isotp_options options;
    uint64_t last_action_tick=0;
    uint64_t this_tick=0;
    ActualState actual_state=ActualState::Sleeping;
    uds_buffer receive_buffer;
    uds_buffer send_buffer;
    unsigned char telegrambuffer[8] ;
    int actual_telegram_pos;
    int actual_send_pos;
    int actual_send_buffer_size;
    int actual_receive_pos;
    int expected_receive_buffer_size;
    int actual_cf_count;
    int flow_control_block_size;
    int consecutive_frame_delay;

public:
    Isotp_Listener(isotp_options options);
    void tick(uint64_t time_ticks);
    int eval_msg(int can_id, unsigned char data[8], int len);
private:
    int copy_to_telegram_buffer();
    int read_from_can_msg(unsigned char data[8], int start, int len);
    void send_cf_telegram();
    void handle_received_message(int len);
};
#endif