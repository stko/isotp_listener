'''
this is the python aquivalent to the c version on https:#github.com/stko/isotp_listener

uds state machine inspired by https:#github.com/stko/oobd/blob/master/interface/OOBD/v1/odp_uds.c

see also: https:#www.embeddeers.com/knowledge-area/jro-can-isotp-einfach-erklaert/
'''

from typing import *

UDS_BUFFER_SIZE = 4095
uds_buffer=bytearray(UDS_BUFFER_SIZE)

class RequestType:
    Service=0
    FlowControl=1

class FrameType:
    Single=0
    First=1
    Consecutive=2
    FlowControl=3

class ActualState:
    Sleeping=0
    First=1
    Consecutive=2
    WaitConsecutive=3
    FlowControl=4


# eval_msg return codes
MSG_NO_UDS= 0 # no uds message, should be proceded by the application
MSG_UDS_OK =1 # successfully handled by isotp_listener
MSG_UDS_WRONG_FORMAT = -1 # message format out of spec
MSG_UDS_UNEXPECTED_CF = -2 # not wating for a CF
MSG_UDS_ERROR = -3 # unclear error


# structure to initialize the isotp_listener constructor
class IsoTpOptions:
    source_address=0
    target_address=0
    bs=0 # The block size sent in the flow control message. Indicates the number of consecutive frame a sender can send before the socket sends a new flow control. A block size of 0 means that no additional flow control message will be sent (block size of infinity)
    stmin =0 # The minimum separation time sent in the flow control message. Indicates the amount of time to wait between 2 consecutive frame. This value will be sent as is over CAN. Values from 1 to 127 means milliseconds. Values from 0xF1 to 0xF9 means 100us to 900us. 0 Means no timing requirements
    frame_timeout = 100 # maximal allowed time in ms between two received frames to keep the transfer active
    wftmax = 0 # Maximum number of wait frame (flow control message with flow status=1) allowed before dropping a message. 0 means that wait frame are not allowed
    send_frame = None
    uds_handler= None


# a (growing) list of UDS services
class Service:
    ClearDTCs= 0x14
    ReadDTC= 0x19


# the Isotp_Listener class
class Isotp_Listener:

    def __init__(self,options):

        self.options = options
        self.last_action_tick=0
        self.this_tick=0
        self.actual_state=ActualState.Sleeping
        self.receive_buffer=bytearray(UDS_BUFFER_SIZE)
        self.send_buffer=bytearray(UDS_BUFFER_SIZE)
        self.telegrambuffer=bytearray(8)
        self.actual_telegram_pos=0
        self.actual_send_pos=0
        self.actual_send_buffer_size=0
        self.actual_receive_pos=0
        self.expected_receive_buffer_size=0
        self.actual_cf_count=0
        self.receive_cf_count=0
        self.flow_control_block_size=0
        self.receive_flow_control_block_count=0
        self.consecutive_frame_delay=0
        self.last_frame_received_tick=0

    def update_options(self, options: IsoTpOptions):
        self.options=options

    def get_options(self):
        return self.options

    def tick(self, time_ticks:int):
        self.this_tick = time_ticks
        if self.actual_state == ActualState.Consecutive:
            if self.last_action_tick + self.consecutive_frame_delay < self.this_tick:
                # it is time to send the next CF
                self.send_cf_telegram()
            return False
        # are we waiting for something?
        if self.actual_state == ActualState.FlowControl or self.actual_state == ActualState.WaitConsecutive:
            if self.last_frame_received_tick + self.options.frame_timeout < self.this_tick:
            # waited too long
                print("Tick Timeout")
                self.actual_state = ActualState.Sleeping
                return True
            return False
        return False


    # transfers data from the send buffer into the can message and set all data accordingly
    def copy_to_telegram_buffer(self):
        nr_of_bytes = 0
        while self.actual_telegram_pos < 8 and self.actual_send_pos < self.actual_send_buffer_size:
            self.telegrambuffer[self.actual_telegram_pos] = self.send_buffer[self.actual_send_pos]
            nr_of_bytes+=1
            self.actual_telegram_pos+=1
            self.actual_send_pos+=1
        for i in range( self.actual_telegram_pos,8):
            self.telegrambuffer[i] = 0 # fill padding bytes
        return nr_of_bytes


    # read data from the can message into the receive buffer and set all data accordingly
    def read_from_can_msg(self, data: bytearray, start : int, nr_of_bytes: int):
        bytes_read = 0
        while nr_of_bytes > 0 and self.actual_receive_pos < UDS_BUFFER_SIZE and len(data) > start and start < 8:
            self.receive_buffer[self.actual_receive_pos] = data[start]
            bytes_read+=1
            start+=1
            self.actual_receive_pos+=1
            nr_of_bytes-=1
        return bytes_read

    # sent next consecutive frame and set all data accordingly
    def send_cf_telegram(self):
        self.telegrambuffer[0] = 0x20 | self.actual_cf_count # single frame
        self.actual_cf_count
        self.actual_cf_count = (self.actual_cf_count+1) & 0x0F
        nr_of_bytes = 1
        self.actual_telegram_pos = 1 # the first byte is already used
        bytes_of_message = self.copy_to_telegram_buffer()
        nr_of_bytes = nr_of_bytes + bytes_of_message
        self.options.send_frame(self.options.target_address, self.telegrambuffer, 8)
        self.last_action_tick = self.this_tick # remember the time of this action
        if self.actual_send_pos >= self.actual_send_buffer_size:
            # buffer is fully send, job done
            print(self.actual_send_buffer_size,"Bytes sent")
            self.actual_state = ActualState.Sleeping # stop all activities
            return
        if self.flow_control_block_size > -1:
            # there's a block size given
            self.flow_control_block_size-=1
            if self.flow_control_block_size < 1:
                # number of allowed CFs sent, waiting for another flow control to continue
                self.actual_state = ActualState.FlowControl
    
    def send_telegram(self, data:bytearray,nr_of_bytes:int):
        if nr_of_bytes>UDS_BUFFER_SIZE or len(data)< nr_of_bytes:
            print(f"ERROR: data size too big with {nr_of_bytes} Bytes")
        for i in range(nr_of_bytes):
            self.send_buffer[i]=data[i]
        self.actual_send_buffer_size=nr_of_bytes
        self.buffer_tx()
        
    def buffer_tx(self):
        if self.actual_send_buffer_size:
            if self.actual_send_buffer_size < 8:               # fits into a single frame
                #generate single frame
                self.telegrambuffer[0] = self.actual_send_buffer_size # single frame
                nr_of_bytes = 1
                self.actual_telegram_pos = 1 # the first byte is already used
                self.actual_send_pos = 0
                nr_of_bytes = nr_of_bytes + self.copy_to_telegram_buffer()
                self.options.send_frame(self.options.target_address, self.telegrambuffer, nr_of_bytes)
            else:
                # generate first frame...
                self.telegrambuffer[0] = 0x10 | self.actual_send_buffer_size >> 8
                self.telegrambuffer[1] = self.actual_send_buffer_size & 0xFF
                nr_of_bytes = 2
                self.actual_telegram_pos = 2 # the first two bytes are already used
                self.actual_send_pos = 0
                nr_of_bytes = nr_of_bytes + self.copy_to_telegram_buffer()
                self.options.send_frame(self.options.target_address, self.telegrambuffer, nr_of_bytes)
                self.actual_state = ActualState.FlowControl # wait for flow control

    def handle_received_message(self,  nr_of_bytes: int):
        print(nr_of_bytes, "Bytes received")
        self.actual_state = ActualState.Sleeping # actual not more to be done
        self.actual_send_buffer_size= self.options.uds_handler(RequestType.Service, self.receive_buffer, nr_of_bytes, self.send_buffer)
        print("Answer with" ,self.actual_send_buffer_size,"Bytes")
        self.buffer_tx()
    '''
    checks, if the given can message is a isotp message.

    returns MSG_xx error codes
    '''
    def eval_msg(self, can_id : int, data : bytearray, nr_of_bytes: int):
        if can_id != self.options.source_address:
            return MSG_NO_UDS
        if nr_of_bytes < 1:
            return MSG_UDS_WRONG_FORMAT #  illegal format
        frame_identifier = data[0] >> 4
        dl = data[0] & 0x0F
        send_len = 0
        if frame_identifier > 3:
            return MSG_UDS_WRONG_FORMAT #  illegal format
        frametype = frame_identifier
        # remember that a valid frame came in
        self.last_frame_received_tick = self.this_tick # remember the time of this action
  
        if frametype == FrameType.First:
            print("First Frame")
            dl = (data[0] & 0x0F) * 256 + data[1]
            # initialize receive parameters
            self.actual_receive_pos = 0
            self.receive_cf_count = 1
            self.expected_receive_buffer_size = dl

            # store the first received bytes in the receive buffer
            if dl> 6:
                dl=6 #  just in case of a spec. violation, but a first frame could contain only a short msg, so check the dl here
            self.read_from_can_msg(data, 2, dl) 

            # send flow control
            self.telegrambuffer[0] = 0x30          # FS Flow Status 0= CLear to Send
            self.telegrambuffer[1] = self.options.bs    # BS Block Size
            self.telegrambuffer[2] = self.options.stmin #  ST min. Separation Time
            self.options.send_frame(self.options.target_address, self.telegrambuffer, 3)
            self.receive_flow_control_block_count = self.options.bs
            if self.receive_flow_control_block_count == 0:
                self.receive_flow_control_block_count = -1
            self.actual_state = ActualState.WaitConsecutive #  wait for Consecutive Frames
        if frametype == FrameType.FlowControl:
            flow_status = data[0] & 0x0F
            print("Flow Control\n")
            if flow_status == 1:
                # wait
                return MSG_UDS_OK #  do nothing
            if flow_status == 2:
                # Overflow - transmission crashed, go back into sleep mode
                self.actual_state = ActualState.Sleeping #  stop all activities
                return MSG_UDS_OK                    # do nothing
            if flow_status == 3:
                # undefined
                self.actual_state = ActualState.Sleeping #  stop all activities
                return MSG_UDS_WRONG_FORMAT          # do nothing
            # the flow status is 0 = Clear to send
            # store parameters
            self.flow_control_block_size = data[1]
            if self.flow_control_block_size == 0:
                # we use -1 as indicator that there's no block size given
                self.flow_control_block_size = -1
            self.consecutive_frame_delay = data[2] #  what's the time unit? For now assuming milliseconds..
            self.actual_cf_count = 1
            # and start sending with the next tick
            self.actual_state = ActualState.Consecutive
            return MSG_UDS_OK
        if frametype == FrameType.Single:
            print("Single Frame")
            self.actual_receive_pos = 0
            if self.read_from_can_msg(data, 1, dl):
                self.handle_received_message(dl)
            self.actual_state = ActualState.Sleeping # stop all activities
            return MSG_UDS_OK                    # message handled
        if frametype == FrameType.Consecutive:
            # print("Consecutive Frame",data[0] & 0x0F,self.receive_flow_control_block_count)
            if self.actual_state == ActualState.WaitConsecutive:
                if self.receive_cf_count != (data[0] & 0x0F):
                    print("wrong CF sequence number")
                    # send cancelation flow control
                    self.telegrambuffer[0] = 0x32 # FS Flow Status 2= Overflow
                    self.telegrambuffer[1] = 0
                    self.telegrambuffer[2] = 0
                    self.options.send_frame(self.options.target_address, self.telegrambuffer, 3)
                    return MSG_UDS_UNEXPECTED_CF
                self.receive_cf_count = (self.receive_cf_count +1) & 0x0F
                if self.read_from_can_msg(data, 1, self.expected_receive_buffer_size - self.actual_receive_pos):
                    if (self.actual_receive_pos == self.expected_receive_buffer_size): # full message received
                        self.actual_state = ActualState.Sleeping# stop all activities
                        self.handle_received_message(self.expected_receive_buffer_size)
                        return MSG_UDS_OK # message handled
                    
                    if self.receive_flow_control_block_count > -1:
                        # there's a limit set
                        if self.receive_flow_control_block_count > 0:
                            self.receive_flow_control_block_count-=1
                        if self.receive_flow_control_block_count == 0:
                            # send another flow control
                            self.telegrambuffer[0] = 0x30          # FS Flow Status 0= CLear to Send
                            self.telegrambuffer[1] = self.options.bs    # BS Block Size
                            self.telegrambuffer[2] = self.options.stmin # ST min. Separation Time
                            self.options.send_frame(self.options.target_address, self.telegrambuffer, 3)
                            self.receive_flow_control_block_count = self.options.bs
                            if self.receive_flow_control_block_count == 0:
                                self.receive_flow_control_block_count = -1
                        return MSG_UDS_OK # message handled
                else: # something went wrong...
                    self.actual_state = ActualState.Sleeping # stop all activities
                    return MSG_UDS_WRONG_FORMAT          # illegal format
            else:
                print("unexpected CF")
                # send cancelation flow control
                self.telegrambuffer[0] = 0x32 # FS Flow Status 2= Overflow
                self.telegrambuffer[1] = 0
                self.telegrambuffer[2] = 0
                self.options.send_frame(self.options.target_address, self.telegrambuffer, 3)
                return MSG_UDS_UNEXPECTED_CF
        return MSG_UDS_ERROR # message handled


    # True if a transfer is actual ongoing
    def busy(self):
        return self.actual_state != ActualState.Sleeping
