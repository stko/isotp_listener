/*

isotp_listener Demo
https://github.com/stko/isotp_listener


a little application listening on socketcan on vcan0. Each received can message is passed to isotp_listener, so
that isotp_listener can handle all incoming uds messages.

To allow isotp_listener the whole message handling, udslisten.tick(timeSinceEpochMillisec()) need to be called all
few milliseconds.

Whenever isotp_listener finds an incoming uds request, it calls the callback function to let the application react on
the request and to provide an answer

Credits:
cansocket routines taken from https://github.com/craigpeacock/CAN-Examples
most uds stuff are taken over from https://github.com/stko/oobd/


*/

// standard includes
#include <iostream>
#include <iomanip>
#include <cstring>
#include <memory>

// network socket stuff
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

// timing
#include <chrono>
#include <thread>

// socketcan
#include <linux/can.h>
#include <linux/can/raw.h>

// isotp_listener itself
#include "isotp_listener.h"

// the global socket
int can_socket = -1;

// little helper for error handling
int guard(int n, const char *err)
{
  if (n == -1)
  {
    perror(err);
    exit(1);
  }
  return n;
}

// get the actual system ticks as milliseconda
uint64_t timeSinceEpochMillisec()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// creates the can socket
int create_can_socket(const char *name, int timeout_ms)
{
  int sockfd;
  struct sockaddr_can addr;
  struct ifreq ifr;

  sockfd = guard(socket(PF_CAN, SOCK_RAW, CAN_RAW), "can't open Socket");
  int flags = guard(fcntl(sockfd, F_GETFL), "could not get flags on TCP listening socket");
  guard(fcntl(sockfd, F_SETFL, flags | O_NONBLOCK), "could not set TCP listening socket to be non-blocking");

  std::strcpy(ifr.ifr_name, name);
  ioctl(sockfd, SIOCGIFINDEX, &ifr);

  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  guard(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)), "Bind error");

  return sockfd;
}

// returns a tuple of a received frame and True, if valid
std::pair<can_frame, bool> receive(int socket)
{
  struct can_frame frame;
  int count;
  int i;

  count = read(socket, &frame, sizeof(struct can_frame));
  return std::pair<can_frame, bool>(frame, count >= 0);
}

// callback function for istotp_listener to allow to send own can messages
int msg_send(int can_id, unsigned char data[8], int len)
{
  struct can_frame frame;
  frame.can_id = can_id;
  len = len > 8 ? 8 : len;
  frame.can_dlc = len;
  for (int i = 0; i < len; i++)
  {
    frame.data[i] = data[i];
  };

  if (write(can_socket, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
  {
    perror("Can't write to socket");
    return 1;
  }
  return 0;
}

/*
the callback function which is called when isotp_listener has received a complete uds message


uds_handler gets either a flow control request (not supported yet) or a service request, if a complete message has been received

  in case of flow control request the send buffer shall be set as follow:
    * Byte 0 lower Nibble: FS = Flow Status (0 = Clear to send, 1 = Wait, 2 = Overflow)
    * Byte 1: BS = Block Size of maximal continious CFs (max. = 255), 0= no limit
    * Byte 3:ST = min. Separation Time between continious CFs
  in case of service request the send buffer shall be set as follow:
    * Byte 0 = SIDPR (SIDRQ + 0x40) (= Byte 0 of receive buffer)
    * Byte 1 = Sub fn  (= Byte 1 of receive buffer)
    * Byte 2 = DID  (= Byte 2 of receive buffer)
    * Byte 3 .. Byte n : data
  send_len= total len of bytes to send
  if an General Responce error shall be reported, the send buffer shall be set as follow: (https://www.rfwireless-world.com/Terminology/UDS-NRC-codes.html)
  * Byte 0 : 0x7F ( NR_SID General Response Error)
  * Byte 1: SIDRQ (= Byte 0 of receive buffer)
  * Byte 2: NRC : Negative response code
  send_len= 3


uds_handler returns the size of the buffer to be send, 0 if no answer is wanted or possible

*/
int uds_handler(RequestType request_type, uds_buffer receive_buffer, int receive_len, uds_buffer send_buffer)
{
  int send_len = 0;
  if (request_type == RequestType::Service)
  {
    if (receive_buffer[0] == Service::ReadDTC)
    {
      if (receive_buffer[1] == 0x01)
      { // get number of DTCs
        // count DTCs and send back
        // format see here: https://github.com/stko/oobd/blob/master/lua-scripts/CarDTCs.epd/cardtcs.lua#L36
        std::cout << "get number of DTC\n";
      }
      else
      { // report DTCs
        // create an answer as described e.g. in https://piembsystech.com/report-dtc-by-status-mask0x02-0x19-service/
        std::cout << "read DTC\n";
        send_buffer[0] = receive_buffer[0] + 0x40;
        send_buffer[1] = receive_buffer[1];
        send_buffer[2] = receive_buffer[2];
        // for testing purposes, the incoming message is returned here
        send_len = receive_len;
        for (int i = 3; i < send_len; i++)
        {
          send_buffer[i] = receive_buffer[i];
        }
        return send_len;
      }
    }
    if (receive_buffer[0] == Service::ClearDTCs)
    {
      std::cout << "Clear DTC\n";
      // clear Errors
    }
  }
  return send_len; // something went wrong, we should never be here..
}

int main()
{
  int i;
  int last_can_id = 0;
  struct can_frame frame;

  std::cout << "Welcome to the isotp_listender demo\n";
  can_socket = create_can_socket("vcan0", 5000);

  // prepare the options for uds_listener
  isotp_options options;
  options.source_address = 0x7E1;                      // listen on can ID
  options.target_address = options.source_address | 8; // uds answer address
  options.bs = 100;                                    // The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
  options.stmin = 5;                                   // time to wait
  options.send_frame = &msg_send;                   // assign callback function to allow isotp_listener to send messages
  options.uds_handler = &uds_handler;                  // assign callback function to allow isotp_listener to announce incoming requests

  Isotp_Listener udslisten(options); // create the isotp_listener object
  unsigned char data[]="ABCDEFGHIJKLM";
  udslisten.send_telegram(data,sizeof(data));
  while (last_can_id != 0x7ff) // for testing purposes: Loop until a 0x7FF mesage comes in
  {
    std::pair<can_frame, bool> received_frame = receive(can_socket);
    if (received_frame.second) // if a message comes in
    {
      last_can_id = received_frame.first.can_id;
      if (!udslisten.eval_msg(received_frame.first.can_id, received_frame.first.data, received_frame.first.can_dlc))
      {
        // e.g. do the normal application stuff here
      }
    }
    else
    {
      udslisten.tick(timeSinceEpochMillisec()); // tell isotp_listener that some time passed by..
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // sleep a few (5) milliseconds
  }

  // close the socket
  guard(close(can_socket), "Error on closing the socket");

  return 0;
}