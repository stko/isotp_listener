/*

uds state machine inspired by https://github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

see also: https://www.embeddeers.com/knowledge-area/jro-can-isotp-einfach-erklaert/

*/

#include "isotp_listener.h"

#include <iostream>

// Isotp_Listener constructor
Isotp_Listener::Isotp_Listener(isotp_options options) : options(options)
{
}

// periodic tick call to allow consecutive frame generation
void Isotp_Listener::tick(uint64_t time_ticks)
{
  this_tick = time_ticks;
  if (actual_state == ActualState::Consecutive)
  {
    // DEBUG("Tick consecutive\n");
    if (last_action_tick + consecutive_frame_delay < this_tick)
    { // it is time to send the next CF
      send_cf_telegram();
    }
  }
}

// transfers data from the send buffer into the can message and set all data accordingly
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

// read data from the can message into the receive buffer and set all data accordingly
int Isotp_Listener::read_from_can_msg(unsigned char data[8], int start, int len)
{
  int nr_of_bytes = 0;
  while (len > 0 && actual_receive_pos < UDS_BUFFER_SIZE && start < 8)
  {
    receive_buffer[actual_receive_pos] = data[start];
    nr_of_bytes++;
    start++;
    actual_receive_pos++;
    len--;
  }
  return nr_of_bytes;
}

// sent next consecutive frame and set all data accordingly
void Isotp_Listener::send_cf_telegram()
{
  telegrambuffer[0] = 0x20 | actual_cf_count; // single frame
  actual_cf_count = ++actual_cf_count & 0x0F;
  int nr_of_bytes = 1;
  actual_telegram_pos = 1; // the first byte is already used
  int bytes_of_message = copy_to_telegram_buffer();
  nr_of_bytes = nr_of_bytes + bytes_of_message;
  options.send_telegram(options.target_address, telegrambuffer, 8);
  last_action_tick = this_tick; // remember the time of this action
  if (actual_send_pos >= actual_send_buffer_size)
  { // buffer is fully send, job done
    DEBUG(actual_send_buffer_size);
    DEBUG(" Bytes sent\n");
    actual_state = ActualState::Sleeping; // stop all activities
    return;
  }
  if (flow_control_block_size > -1)
  { // there's a block size given
    flow_control_block_size--;
    if (flow_control_block_size < 1)
    { // number of allowed CFs sent, waiting for another flow control to continue
      actual_state = ActualState::FlowControl;
    }
  }
}

/*
 */
void Isotp_Listener::handle_received_message(int len)
{
  DEBUG(len);
  DEBUG(" Bytes received\n");
  actual_state = ActualState::Sleeping; // actual not more to be done
  if (options.uds_handler(RequestType::Service, receive_buffer, len, send_buffer, actual_send_buffer_size))
  {
    DEBUG("Answer with ");
    DEBUG(actual_send_buffer_size);
    DEBUG(" Bytes\n");
    if (actual_send_buffer_size < 8)               // fits into a single frame
    {                                              // generate single frame
      telegrambuffer[0] = actual_send_buffer_size; // single frame
      int nr_of_bytes = 1;
      actual_telegram_pos = 1; // the first byte is already used
      actual_send_pos = 0;
      nr_of_bytes = nr_of_bytes + copy_to_telegram_buffer();
      options.send_telegram(options.target_address, telegrambuffer, nr_of_bytes);
    }
    else
    { // generate first frame...
      telegrambuffer[0] = 0x10 | actual_send_buffer_size >> 8;
      telegrambuffer[1] = actual_send_buffer_size & 0xFF;
      int nr_of_bytes = 2;
      actual_telegram_pos = 2; // the first two bytes are already used
      actual_send_pos = 0;
      nr_of_bytes = nr_of_bytes + copy_to_telegram_buffer();
      options.send_telegram(options.target_address, telegrambuffer, nr_of_bytes);
      actual_state = ActualState::FlowControl; // wait for flow control
    }
  }
}

/* checks, if the given can message is a isotp message.

returns MSG_xx error codes
*/
int Isotp_Listener::eval_msg(int can_id, unsigned char data[8], int len)
{
  if (can_id != options.source_address)
  {
    return MSG_NO_UDS;
  }
  if (len < 1)
  {
    return MSG_UDS_WRONG_FORMAT; // illegal format
  }
  int frame_identifier = data[0] >> 4;
  int dl = (int)data[0] & 0x0F;
  int send_len = 0;
  if (frame_identifier > 3)
  {
    return MSG_UDS_WRONG_FORMAT; // illegal format
  }
  FrameType frametype = static_cast<FrameType>(frame_identifier);

  if (frametype == FrameType::First)
  {
    DEBUG("First Frame\n");
    dl = ((int)data[0] & 0x0F) * 256 + (int)data[1];
    // initialize receive parameters
    actual_receive_pos = 0;
    expected_receive_buffer_size = dl;

    // store the first received bytes in the receive buffer
    read_from_can_msg(data, 2, dl > 6 ? 6 : dl); // just in case of a spec. violation, but a first frame could contain only a short msg, so check the dl here

    // send flow control
    telegrambuffer[0] = 0x30;          // FS Flow Status 0= CLear to Send
    telegrambuffer[1] = options.bs;    // BS Block Size
    telegrambuffer[2] = options.stmin; // ST min. Separation Time
    options.send_telegram(options.target_address, telegrambuffer, 3);
    actual_state = ActualState::WaitConsecutive; // wait for Consecutive Frames
  }
  if (frametype == FrameType::FlowControl)
  {
    unsigned char flow_status = data[0] & 0x0F;
    DEBUG("Flow Control\n");
    if (flow_status == 1)
    {                    // wait
      return MSG_UDS_OK; // do nothing
    }
    if (flow_status == 2)
    {                                       // Overflow - transmission crashed, go back into sleep mode
      actual_state = ActualState::Sleeping; // stop all activities
      return MSG_UDS_OK;                    // do nothing
    }
    if (flow_status == 3)
    {                                       // undefined
      actual_state = ActualState::Sleeping; // stop all activities
      return MSG_UDS_WRONG_FORMAT;          // do nothing
    }
    // the flow status is 0 = Clear to send
    // store parameters
    flow_control_block_size = data[1];
    if (flow_control_block_size == 0)
    { // we use -1 as indicator that there's no block size given
      flow_control_block_size = -1;
    }
    consecutive_frame_delay = data[2]; // what's the time unit? For now assuming milliseconds..
    actual_cf_count = 1;
    // and start sending with the next tick
    actual_state = ActualState::Consecutive;
    return MSG_UDS_OK;
  }
  if (frametype == FrameType::Single)
  {
    DEBUG("Single Frame\n");
    actual_receive_pos = 0;
    if (read_from_can_msg(data, 1, dl))
    {
      handle_received_message(dl);
    }
    actual_state = ActualState::Sleeping; // stop all activities
    return MSG_UDS_OK;                    // message handled
  }
  if (frametype == FrameType::Consecutive)
  {
    // DEBUG("Consecutive Frame\n");
    if (actual_state == ActualState::WaitConsecutive)
    {
      if (read_from_can_msg(data, 1, expected_receive_buffer_size - actual_receive_pos))
      {
        if (actual_receive_pos == expected_receive_buffer_size) // full message received
        {
          actual_state = ActualState::Sleeping; // stop all activities
          handle_received_message(expected_receive_buffer_size);
        }
        return MSG_UDS_OK; // message handled
      }
      else // something went wrong...
      {
        actual_state = ActualState::Sleeping; // stop all activities
        return MSG_UDS_WRONG_FORMAT;          // illegal format
      }
    }
    else
    {
      DEBUG("unexpected CF\n");
      return MSG_UDS_UNEXPECTED_CF;
    }
  }
  return MSG_UDS_ERROR; // message handled
}