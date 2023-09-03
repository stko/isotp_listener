#ifndef ISOTP_LISTENER_H
#define ISOTP_LISTENER_H

#include <cstdint>

#define UDS_BUFFER_SIZE 4096

typedef unsigned char uds_buffer[UDS_BUFFER_SIZE];

enum class RequestType { Single, First, Consecutive, FlowControl };
enum class ActualState { Sleeping, First, Consecutive, FlowControl };

struct isotp_options
{
    int source_address=0;
    int target_address=0;
    int bs=0 ; // The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
    int stmin =0 ; // The minimum separation time sent in the flow control message. Indicates the amount of time to wait between 2 consecutive frame. This value will be sent as is over CAN. Values from 1 to 127 means milliseconds. Values from 0xF1 to 0xF9 means 100us to 900us. 0 Means no timing requirements
    int wftmax = 0 ;// Maximum number of wait frame (flow control message with flow status=1) allowed before dropping a message. 0 means that wait frame are not allowed
    int(*send_telegram)(int,unsigned char[8],int len)=0;
    int(*uds_handler)(RequestType request_type , unsigned char[7])=0;
    uint64_t last_tick=0;
    ActualState actual_state=ActualState::Sleeping;
    uds_buffer send_buffer;
    uds_buffer receive_buffer;

};

class Isotp_Listener
{
private:
    isotp_options options;

public:
    Isotp_Listener(isotp_options options);
    void tick(uint64_t time_ticks);
    bool eval_msg(int can_id, unsigned char data[8], int len, uint64_t ticks);
};
#endif