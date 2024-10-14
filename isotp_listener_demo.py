'''
isotp_listener Demo
this is the python aquivalent to the c version on https://github.com/stko/isotp_listener


a little application listening on socketcan on vcan0. Each received can message is passed to isotp_listener, so
that isotp_listener can handle all incoming uds messages.

To allow isotp_listener the whole message handling, udslisten.tick(timeSinceEpochMillisec()) need to be called all 
few milliseconds.

Whenever isotp_listener finds an incoming uds request, it calls the callback function to let the application react on
the request and to provide an answer

Credits:
cansocket routines taken from https://github.com/craigpeacock/CAN-Examples
most uds stuff are taken over from https://github.com/stko/oobd/
'''


# standard includes
# network socket stuff
# timing
import time

# socketcan
import can

# isotp_listener itself
import isotp_listener

# the global socket
bustype = 'socketcan'
channel = 'vcan0'




# callback function for istotp_listener to allow to send own can messages
def msg_send(can_id : int, data :bytearray,len: int, is_extended=False):
    try:
        msg = can.Message(arbitration_id=can_id, data=data[:len], is_extended_id=False)
        bus.send(msg)
        return 0
    except:
        print("Can't write to socket")
        return 1


'''
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

uds_handler returns true if the service shall be further proceed, otherways return false
'''


def uds_handler(request_type: isotp_listener.RequestType,  receive_buffer:isotp_listener.uds_buffer,  receive_len : int,  send_buffer : isotp_listener.uds_buffer):
    send_len=0
    if request_type == isotp_listener.RequestType.Service:
        if receive_buffer[0] == isotp_listener.Service.ReadDTC:

            if receive_buffer[1] == 0x01:
                # get number of DTCs
                # count DTCs and send back
                # format see here: https://github.com/stko/oobd/blob/master/lua-scripts/CarDTCs.epd/cardtcs.lua#L36
                print ("get number of DTC")
            else:
                # report DTCs
                # create an answer as described e.g. in https://piembsystech.com/report-dtc-by-status-mask0x02-0x19-service/
                print ("read DTC")
                print ("request:",' '.join('{:02x}'.format(x) for x in receive_buffer[:receive_len]))

                send_buffer[0] = receive_buffer[0] + 0x40
                send_buffer[1] = receive_buffer[1]
                send_buffer[2] = receive_buffer[2]
                # for testing purposes, the incoming message is returned here
                send_len = receive_len
                for i in range(3, send_len):
                    send_buffer[i] = receive_buffer[i]
                print ("answer:",' '.join('{:02x}'.format(x) for x in send_buffer[:send_len]))
                return send_len
        if receive_buffer[0] == isotp_listener.Service.ClearDTCs:
            print("Clear DTC")
        # clear Errors
    return send_len; # something went wrong, we should never be here..


print("Welcome to the isotp_listender demo")
# creates the can socket
bus = can.Bus(channel=channel, interface=bustype)

# prepare the options for uds_listener
options = isotp_listener.IsoTpOptions()
options.source_address = 0x7E1 # listen on can ID 
options.target_address = options.source_address | 8 # uds answer address
options.bs=10 # The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
options.stmin =5 # time to wait
options.send_frame = msg_send # assign callback function to allow isotp_listener to send messages
options.uds_handler = uds_handler # assign callback function to allow isotp_listener to announce incoming requests

udslisten = isotp_listener.Isotp_Listener (options)  # create the isotp_listener object



last_can_id =0
data=bytearray(b"ABCDEFGHIJK")
udslisten.send_telegram(data,len(data))
while last_can_id != 0x7ff: # for testing purposes: Loop until a 0x7FF mesage comes in
    received_frame = bus.recv(timeout=0.01)
    if received_frame: # if a message comes in
        last_can_id = received_frame.arbitration_id
        if not udslisten.eval_msg(received_frame.arbitration_id, received_frame.data, received_frame.dlc):
            # e.g. do the normal application stuff here
            pass
    else:
        udslisten.tick(time.time()) # tell isotp_listener that some time passed by..
    time.sleep(0.005) #  sleep a few (5) milliseconds

bus.close()