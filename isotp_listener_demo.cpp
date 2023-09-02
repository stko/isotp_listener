#include <iostream>
#include <cstring>
#include <memory>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "isotp_listener.h"

int create_can_send_socket(const char *name, int timeout_ms)
{
  int sockfd;
  struct sockaddr_can addr;
  struct ifreq ifr;

  if ((sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
  {
    perror("Socket");
    return 1;
  }

// LINUX
struct timeval tv;
tv.tv_sec = timeout_ms / 1000;
tv.tv_usec = timeout_ms % 1000;
setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);


  std::strcpy(ifr.ifr_name, name);
  ioctl(sockfd, SIOCGIFINDEX, &ifr);

  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    perror("Bind");
    return -1;
  }
  return sockfd;
}

int main()
{

	int i; 
	int nbytes;

  int socket_send = create_can_send_socket("vcan0",5000);

  struct can_frame frame;

  std::cout << "Willkommen"
            << " bei C++" << '!' << "\n";

	frame.can_id = 0x555;
	frame.can_dlc = 5;
	frame.data[0]=1;

	if (write(socket_send, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
		perror("Write");
		return 1;
	}

	nbytes = read(socket_send, &frame, sizeof(struct can_frame));

 	if (nbytes < 0) {
		perror("Read");
		return 1;
	}

	if (close(socket_send) < 0) {
		perror("Close");
		return 1;
	}


	printf("0x%03X [%d] ",frame.can_id, frame.can_dlc);

	for (i = 0; i < frame.can_dlc; i++)
		printf("%02X ",frame.data[i]);





	return 0;

  Isotp_Listener myObj(2023, 9, 2); // Create an object of MyClass

  // Print attribute values
  std::cout << myObj.getYear() << "\n";
  std::cout << myObj.getMonth() << "\n";
  return 0;
}