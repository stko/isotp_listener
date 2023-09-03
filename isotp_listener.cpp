/*

uds state machine from https://github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

see also: https://www.embeddeers.com/knowledge-area/jro-can-isotp-einfach-erklaert/

*/

#include "isotp_listener.h"

// Isotp_Listener constructor
Isotp_Listener::Isotp_Listener(isotp_options options) : options(options)
{
  unsigned char data[] = {7, 6, 5, 4, 3, 2, 1, 0};
  options.send_telegram(options.target_address, data, 8);
}

// Isotp_Listener member function
void Isotp_Listener::tick(uint64_t time_ticks)
{
}

/* checks, if the given can message is a valid isotp message.

If yes, it's proceeded and the function returns true as sign to not further procced the message, otherways false
*/
bool Isotp_Listener::eval_msg(int can_id, unsigned char data[8], int len, uint64_t ticks)
{

  if (can_id != options.source_address)
  {
    return false;
  }
  if (len < 1)
  {
    return false; // illegal format
  }
  int frame_identifier = data[0] >> 4;
  int dl = data[0] >> 4;
  if (frame_identifier > 3)
  {
    return false; // illegal format
  }
  if (dl > 7)
  {
    return false; // illegal format
  }
  unsigned char request[7];
  for (int i = 0; i < 6; i++)
  {
    request[i] = data[i + 2];
  }
  RequestType requesttype = static_cast<RequestType>(frame_identifier);

  if (requesttype == RequestType::First)
  {
    dl=data[0] &3 *256 +  data[1];
  }
  if (requesttype == RequestType::Single)
  {
    actual_state=ActualState::Sleeping; // reset all activities
    options.uds_handler(RequestType::Single, request);
    tick(ticks);
    return true; // message handled

  }
}