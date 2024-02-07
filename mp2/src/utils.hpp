#include <cstdlib>
#include <cstdint>
#include <chrono>

#ifndef UTILS_HPP
#define UTILS_HPP

#define MSS 1460
#define TIMEOUT 150
#define SOCKET_TIMEOUT 100000
#define INIT_SSTHRESH 512
#define MAX_RECEIVER_QUEUE_SIZE 1000
enum packet_type{
    SYN,
    SYNACK,
    ACK,
    MSG,
    FIN,
    FINACK
};

enum sender_state {
    SLOW_START = 20,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
};

typedef struct {
    uint64_t seq_num;
    uint64_t ack_num;
    packet_type type;
    int data_len;
    std::chrono::system_clock::time_point timestamp;
}packet_header;

typedef struct {
    packet_header header;
    char payload[MSS];
}packet;

#endif