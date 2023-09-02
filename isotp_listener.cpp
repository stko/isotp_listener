/*

uds state machine from https://github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

*/

#include "isotp_listener.h"

// Isotp_Listener constructor
Isotp_Listener::Isotp_Listener(int physical_address, int functional_address, int(*send_telegram)(int,unsigned char[8],int len)):
physical_address (physical_address),
functional_address(functional_address),
send_telegram(send_telegram)
{
 unsigned char data[] = {7, 6, 5, 4, 3, 2, 1, 0};
  send_telegram(physical_address, data, 8);

}

// Isotp_Listener member function
void Isotp_Listener::tick(int time_ticks)
{
}