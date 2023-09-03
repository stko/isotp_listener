/*

isotp_listener Demo
https://github.com/stko/isotp_listener

cansocket routines taken from https://github.com/craigpeacock/CAN-Examples


*/

// standards
#include <iostream>
#include <iomanip>
#include <cstring>
#include <memory>

// socket stuff
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

// timing
#include <chrono>
#include <thread>

// socket can
#include <linux/can.h>
#include <linux/can/raw.h>

// isotp_listener itself
#include "isotp_listener.h"

// the global socket

int socket_send = -1;

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

uint64_t timeSinceEpochMillisec()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// creates the socket can socket
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
  if (count < 0)
  {
    return std::pair<can_frame, bool>(frame, false);
  }

  std::cout << std::hex
            << std::uppercase
            << std::setw(4)
            << std::setfill('0')
            << frame.can_id
            << " ["
            << (int)frame.can_dlc
            << "] ";

  for (i = 0; i < frame.can_dlc; i++)
    std::cout << std::hex
              << std::uppercase
              << std::setw(2)
              << std::setfill('0')
              << (int)frame.data[i]
              << " ";
  std::cout << "\n";
  return std::pair<can_frame, bool>(frame, true);
}

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

  if (write(socket_send, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame))
  {
    perror("Can't write to socket");
    return 1;
  }
  return 0;
}

int uds_handler(RequestType request_type , unsigned char request[7]){
  std::cout << "bla\n";
}

int main()
{

  int i;
  int nbytes;
  int last_can_id = 0;

  socket_send = create_can_socket("vcan0", 5000);

  struct can_frame frame;

  std::cout << "Welcome\n";
  isotp_options options;
  options.source_address = 0x7E1;
  options.target_address = options.source_address | 8;
  options.send_telegram = &msg_send;
  options.uds_handler = &uds_handler;
  Isotp_Listener udslisten(options);

  while (last_can_id != 0x7ff)
  {

    std::pair<can_frame, bool> received_frame = receive(socket_send);
    if (received_frame.second)
    {
      last_can_id = received_frame.first.can_id;
      std::cout << "frame id 0x" << last_can_id << " " << 0x7ff << "\n";
      udslisten.eval_msg(received_frame.first.can_id, received_frame.first.data, received_frame.first.can_dlc, timeSinceEpochMillisec());
    }else{
      udslisten.tick(timeSinceEpochMillisec());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // close the socket
  if (close(socket_send) < 0)
  {
    perror("Close");
    return 1;
  }

  return 0;
}