/*

uds state machine from https://github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

see also: https://www.embeddeers.com/knowledge-area/jro-can-isotp-einfach-erklaert/

*/

#include "isotp_listener.h"

#include <iostream>

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

int Isotp_Listener::copy_to_telegram_buffer()
{
  int nr_of_bytes = 0;
  while (actual_telegram_pos < 8 && actual_send_pos < actual_send_buffer_size)
  {
    telegrambuffer[actual_telegram_pos] = send_buffer[actual_send_pos];
    nr_of_bytes++;
    actual_telegram_pos++;
    actual_send_pos++;
  }
  for (int i = actual_telegram_pos; i < 8; i++)
  {
    telegrambuffer[i] = 0; // fill padding bytes
  }
  return nr_of_bytes;
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
  std::cout << "\nschwupti " << (int)data[0] << "\n";
  int dl = (int)data[0] & 7;
  std::cout << "\nschwupti " << dl << "\n";
  int send_len = 0;
  if (frame_identifier > 3)
  {
    return false; // illegal format
  }
  for (int i = 0; i < dl; i++)
  {
    receive_buffer[i] = data[i + 1];
  }
  FrameType frametype = static_cast<FrameType>(frame_identifier);

  if (frametype == FrameType::First)
  {
    dl = ((int)data[0] & 3) * 256 + (int)data[1];

    senden des flow controls & warten auf CFs
  }
  if (frametype == FrameType::FlowControl)
  {
    werte speichern und beginnen mit dem Senden

  }
  if (frametype == FrameType::Single)
  {
    actual_state = ActualState::Sleeping; // reset all activities
    if (options.uds_handler(RequestType::Service, receive_buffer, dl, send_buffer, actual_send_buffer_size))
    {
      if (actual_send_buffer_size < 8) // fits into a single frame
      {
        telegrambuffer[0] = actual_send_buffer_size; // single frame
        int nr_of_bytes = 1;
        actual_telegram_pos = 1; // the first byte is already used
        actual_send_pos=0;
        nr_of_bytes = nr_of_bytes + copy_to_telegram_buffer();
        options.send_telegram(options.target_address, telegrambuffer, nr_of_bytes);
      }else{ // first frame...
        telegrambuffer[0]=0x10 | actual_send_buffer_size >> 8;
        telegrambuffer[1]= actual_send_buffer_size & 0xFF;
        int nr_of_bytes = 2;
        actual_telegram_pos = 2; // the two bytes are already used
        actual_send_pos=0;
        nr_of_bytes = nr_of_bytes + copy_to_telegram_buffer();
        options.send_telegram(options.target_address, telegrambuffer, nr_of_bytes);
        actual_state = ActualState::FlowControl; // wait for flow control     
      }
    }
    tick(ticks);
    return true; // message handled
  }
}