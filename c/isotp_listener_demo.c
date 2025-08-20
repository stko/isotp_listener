#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>

#include "isotp_listener.h"

#define CAN_ID_EXT_FLAG 0x80000000U
#define CAN_ID_RTR_FLAG 0x40000000U
#define CAN_ID_ERR_FLAG 0x20000000U

// The global CAN socket file descriptor
int s;

// Helper function to get current time in milliseconds
long long get_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000;
}

// Callback function for isotp_listener to send CAN messages
void msg_send(uint32_t can_id, uint8_t *data, size_t len) {
    struct can_frame frame;

    frame.can_id = can_id;
    frame.can_dlc = len;
    memcpy(frame.data, data, len);

    if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        perror("Error writing to CAN socket");
    }
}

// The callback function which is called when isotp_listener has received a complete UDS message
size_t uds_handler(unsigned char request_type, uint8_t *receive_buffer, size_t receive_len, uint8_t *send_buffer) {
    size_t send_len = 0;

    if (request_type == requestType.Service) {
        if (receive_buffer[0] == 0x19) { // Using magic number 0x19 for Service.ReadDTC
            if (receive_buffer[1] == 0x01) {
                printf("get number of DTC\n");
            } else {
                printf("read DTC\n");
                printf("request:");
                for (int i = 0; i < receive_len; i++) {
                    printf(" %02x", receive_buffer[i]);
                }
                printf("\n");

                send_buffer[0] = receive_buffer[0] + 0x40;
                send_buffer[1] = receive_buffer[1];
                send_buffer[2] = receive_buffer[2];
                send_len = receive_len;
                for (int i = 3; i < send_len; i++) {
                    send_buffer[i] = receive_buffer[i];
                }
                printf("answer:");
                for (int i = 0; i < send_len; i++) {
                    printf(" %02x", send_buffer[i]);
                }
                printf("\n");
                return send_len;
            }
        } else if (receive_buffer[0] == 0x14) { // Using magic number 0x14 for Service.ClearDTCs
            printf("Clear DTC\n");
        }
    }

    return send_len;
}

int main(void) {
    printf("Welcome to the isotp_listender demo\n");

    // CAN socket setup
    struct sockaddr_can addr;
    struct ifreq ifr;

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Error creating socket");
        return 1;
    }

    strcpy(ifr.ifr_name, "vcan0");
    ioctl(s, SIOCGIFINDEX, &ifr);

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Error binding socket");
        return 1;
    }

    // Prepare the options for uds_listener
    IsoTpOptions options={
    .source_address = 0x7E1,
    .target_address = options.source_address | 8,
    .bs = 10,
    .stmin = 5,
    .send_frame = msg_send,
    .uds_handler = uds_handler,
    .frame_timeout = 100,
    .wftmax = 0,
    };
    struct Isotp_Listener udslisten;
    Isotp_Listener_init(&udslisten, options);

    // Initial send for demo purposes
    uint8_t data[] = "ABCDEFGHIJK";
    send_telegram(&udslisten, data, sizeof(data) - 1);

    uint32_t last_can_id = 0;
    struct can_frame frame;
    fd_set rdfs;
    struct timeval tv;
    int retval;
    long long current_time_ms;

    while (last_can_id != 0x7FF) {
        // Use select to wait for data on the socket with a timeout
        FD_ZERO(&rdfs);
        FD_SET(s, &rdfs);
        tv.tv_sec = 0;
        tv.tv_usec = 5000; // 5 milliseconds (0.005 seconds)

        retval = select(s + 1, &rdfs, NULL, NULL, &tv);

        current_time_ms = get_millis();
        
        if (retval > 0 && FD_ISSET(s, &rdfs)) { // A message was received
            if (read(s, &frame, sizeof(struct can_frame)) < 0) {
                perror("Error reading from CAN socket");
            } else {
                last_can_id = frame.can_id;
                if (eval_msg(&udslisten, frame.can_id, frame.data, frame.can_dlc) == MSG_NO_UDS) {
                    // Do normal application stuff here
                }
            }
        } else { // Timeout occurred, no message received
            tick(&udslisten, current_time_ms);
        }
    }

    close(s);

    return 0;
}