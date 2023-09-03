#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

#include <cstdint>

#define UDS_BUFFER_SIZE 4096

typedef unsigned char uds_buffer[UDS_BUFFER_SIZE];

enum class RequestType { Service, FlowControl };
enum class FrameType { Single, First, Consecutive, FlowControl };
enum class ActualState { Sleeping, First, Consecutive, FlowControl };

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

class Service
{
    public:
    static unsigned char const ReadDTC= 0x19;
} ;



class Isotp_Listener
{
private:
    isotp_options options;
    uint64_t last_tick=0;
    ActualState actual_state=ActualState::Sleeping;
    uds_buffer receive_buffer;
    uds_buffer send_buffer;
    unsigned char telegrambuffer[8] ;
    int actual_telegram_pos;
    int actual_send_pos;
    int actual_send_buffer_size;
    int actual_cf_count;
    int max_cf_count;
    int consecutive_frame_delay;
    int consecutive_frame_counter;

public:
    Isotp_Listener(isotp_options options);
    void tick(uint64_t time_ticks);
    bool eval_msg(int can_id, unsigned char data[8], int len, uint64_t ticks);
private:
    int copy_to_telegram_buffer();
};
#endif