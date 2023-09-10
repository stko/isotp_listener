# Isotp_Listener

Isotp_Listener hooks itself into the can message flow of an existing application and listens for incoming IsoTP messages. Whenever a complete message is received,
it's announced to the application and an IsoTP answer is returned to the sender.

In opposite to other IsoTP libraries, Isotp_Listener does not act as a standalone send/receive library which occupies the whole application task, it works more as a sidecar and leaves the application untouched and only comes up when a message have been received. It also does not rely on any system specific resources nor it needs e.g. a separate thread.


## Usage

The application need to provide two callbacks:

```
    int(*send_telegram)(int,unsigned char[8],int len)

    bool(*uds_handler)(RequestType request_type , uds_buffer  receive_buffer, int recv_len,  uds_buffer  send_buffer, int & send_len)
```

`send_telegram` is used to send own can messages

`uds_handler` is called whenever a uds messages has been received

The callbacks and a few other values are given to the Isotp_Listener constructor. Isotp_Listener has two methods
```
    void Isotp_Listener::tick(uint64_t time_ticks);

    int Isotp_Listener::eval_msg(int can_id, unsigned char data[8], int len);
```

where `eval_msg` is called by the application with each received can message, and `tick` is called each few milliseconds to allow Isotp_Listener its internal message handling.

## Demo 
The provided demo runs on Linux on the socketcan virtual device vcan0.

With the test command 

```
python3 generate_random_hexdump.py 3000 "19 00 00" | isotpsend -b -s 7e1 -d 7e9 vcan0
```

some random messages can be sent to the demo, the command 

`isotprecv -l -s 7e1  -d 7e9 vcan0`

monitors the isotp messsages, and

`candump -t d vcan0`

shows the raw can data exchange