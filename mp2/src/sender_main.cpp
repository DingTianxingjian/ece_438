/*
 * File:   sender_main.c
 * Author:
 *
 * Created on
 */

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include <iostream>
#include <queue>
#include <math.h>
#include <set>
#include <cmath>
#include "utils.hpp"

using namespace std;

struct sockaddr_in si_other;

int s, slen;

FILE* fp;
sender_state state;
float cwnd;
int ssthresh;
uint64_t dup_ack_num;
uint64_t recv_ack_index;
uint64_t seq_index;
uint64_t total_pkt_num;
uint64_t unsent_bytes;
deque<packet> sent_deque;
deque<packet> prepare_queue;


void diep(char *s) {
    perror(s);
    exit(1);
}

packet build_no_data_pkt(packet_type type) {
    packet p;
    p.header.type = type;
    p.header.data_len = 0;
    p.header.seq_num = 0;
    p.header.ack_num = 0;
    memset(p.payload, 0, MSS);
    return p;
}


void send_one_packet(packet *p) {
    if (sendto(s, p, sizeof(packet), 0, (struct sockaddr*)&si_other, sizeof(si_other))== -1){
        diep("error in send_one_packet");
    }
    //cout << "success send packet" <<p->header.seq_num << endl;
}

void help_print_deque(deque<packet> tmp) {
    for (int i = 0; i < tmp.size(); i++) {
        cout << tmp.front().header.seq_num << "  ";
        tmp.pop_front();
    }
    cout << endl;
}

void send_packets_in_cwnd() {
    //cout << "unsent bytes are " << unsent_bytes << endl;
    if (unsent_bytes == 0) return;
    char payload_buffer[MSS];
    memset(payload_buffer, 0, MSS);
    packet p;

    for (int i = 0; i < ceil(cwnd - sent_deque.size()); ++i) {
        int bytes_limit = min(unsent_bytes, (uint64_t)MSS);
        int data_length = fread(payload_buffer, sizeof(char), bytes_limit, fp);
        if (data_length > 0) {
            p.header.seq_num = seq_index;
            p.header.type = MSG;
            p.header.data_len = data_length;
            p.header.timestamp = std::chrono::system_clock::now();
            memcpy(p.payload, &payload_buffer, data_length);
            seq_index ++;
            unsent_bytes -= data_length;
            prepare_queue.push_back(p);
        }
    }
    while (!prepare_queue.empty()) {
        send_one_packet(&prepare_queue.front());
        sent_deque.push_back(prepare_queue.front());
        prepare_queue.pop_front();
    }
}

void switch_to_fast_recovery() {
    //cout << "switch_to_fast_recovery" << endl;
    state = FAST_RECOVERY;
    ssthresh = (int)(cwnd / 2.0);
    ssthresh = max(ssthresh, 512);
    cwnd = (float)(ssthresh + 3);

    //help_print_deque(sent_deque);

    deque<packet> tmp = sent_deque;
    for (int i = 0; i < 32; i++) {
        if (!tmp.empty()) {
            send_one_packet(&tmp.front());
            tmp.pop_front();
        }
    }
//
//    if (!sent_deque.empty()) {
//        send_one_packet(&sent_deque.front());
//    }
//    dup_ack_num = 0;
}



void handle_timeout() {
    //cout << "timeout!!!" << endl;
    state = SLOW_START;
    ssthresh = (int)(cwnd / 2.0);
    ssthresh = max(ssthresh, 512);
    cwnd = (float)100;
    //help_print_deque(sent_deque);
//
    deque<packet> tmp = sent_deque;
    for (int i = 0; i < 32; i++) {
        if (!tmp.empty()) {
            send_one_packet(&tmp.front());
            tmp.pop_front();
        }
    }
//
//    if (!sent_deque.empty()) {
//        send_one_packet(&sent_deque.front());
//    }
    dup_ack_num = 0;
}

void switch_state() {
    switch (state) {
        case SLOW_START:
            if (dup_ack_num >= 3) {
                switch_to_fast_recovery();
            }
            else if (dup_ack_num == 0) {
                if ((int)cwnd >= ssthresh) {
                    state = CONGESTION_AVOIDANCE;
                    return;
                }
                cwnd += 1;
                cwnd = max((float) 1, cwnd);
            }
            break;
        case CONGESTION_AVOIDANCE:
            if (dup_ack_num >= 3) {
                switch_to_fast_recovery();
            }
            else if (dup_ack_num == 0) {
                cwnd += 1 / floor(cwnd);
                cwnd = max((float) 1, cwnd);
            }
            break;
        case FAST_RECOVERY:
            if (dup_ack_num > 0) {
                cwnd += (float)1;
                return;
            }
            else if (dup_ack_num == 0){
                cwnd = (float)ssthresh;
                cwnd = max((float) 1, cwnd);
                state = CONGESTION_AVOIDANCE;
            }
            break;
        default:
            break;
    }
}

void handle_ack(packet* p) {
    if (p->header.ack_num < sent_deque.front().header.seq_num) {
        //cout << "receive old ack" << p->header.ack_num <<endl;
        dup_ack_num++;
        switch_state();
    }
    else if (p->header.ack_num >= sent_deque.front().header.seq_num) {
        dup_ack_num = 0;
        switch_state();
        //int recv_pkt_num = p->header.seq_num - sent_deque.front().header.seq_num;
        recv_ack_index = p->header.ack_num;
        //cout << recv_ack_index << "recv index in handle ack"<<endl;
        while (!sent_deque.empty() && sent_deque.front().header.seq_num <= recv_ack_index) {
            sent_deque.pop_front();
        }
        send_packets_in_cwnd();
    }
}


void init_settings(unsigned long long int bytesToTransfer) {
    cwnd = 6;
    ssthresh = INIT_SSTHRESH;
    dup_ack_num = 0;
    recv_ack_index = 0;
    seq_index = 0;
    state = SLOW_START;
    total_pkt_num = ceil(bytesToTransfer * 1.0 / MSS) - 1;
    unsent_bytes = bytesToTransfer;
}

void init_sender_tcp() {
    char buf[sizeof(packet)];
    packet p = build_no_data_pkt(SYN);
    send_one_packet(&p);
    slen = sizeof(si_other);
    while (true) {
        if (recvfrom(s, buf, sizeof(packet), 0, (struct sockaddr *)&si_other, (socklen_t*)&slen) == -1) {
            if (errno != EAGAIN || errno != EWOULDBLOCK){
                diep("recvfrom()");
            }
            else{
                cout << "Last SYN timeout" << endl;
                p = build_no_data_pkt(SYN);
                send_one_packet(&p);
            }
        }
        else {
            packet pkt;
            memcpy(&pkt, buf, sizeof(packet));
            if (pkt.header.type == SYNACK) {
                p = build_no_data_pkt(SYNACK);
                send_one_packet(&p);
                break;
            }
        }
    }
}

void terminate_sender_tcp() {
    char buf[sizeof(packet)];
    packet p = build_no_data_pkt(FIN);
    send_one_packet(&p);
    slen = sizeof(si_other);
    while (true) {
        if (recvfrom(s, buf, sizeof(packet), 0, (struct sockaddr *)&si_other, (socklen_t*)&slen) == -1) {
            if (errno != EAGAIN || errno != EWOULDBLOCK){
                diep("recvfrom()");
            }
            else{
                cout << "Last Fin timeout" << endl;
                p = build_no_data_pkt(FIN);
                send_one_packet(&p);
            }
        }
        else {
            packet pkt;
            memcpy(&pkt, buf, sizeof(packet));
            if (pkt.header.type == FINACK) {
                p = build_no_data_pkt(FINACK);
                send_one_packet(&p);
                break;
            }
        }
    }
}
void detect_timeout() {
    packet oldest_pkt = sent_deque.front();
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    if (now - oldest_pkt.header.timestamp > std::chrono::milliseconds(TIMEOUT)) {
        handle_timeout();
    }
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

    /* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    init_settings(bytesToTransfer);
    //set timeout for socket
    struct timeval sock_to;
    sock_to.tv_sec = 0;
    sock_to.tv_usec = SOCKET_TIMEOUT;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &sock_to, sizeof(sock_to)) == -1){
        diep("error in setsockopt");
    }
    // init connection
    //init_sender_tcp();
    /* Send data and receive acknowledgements on s*/
    // main loop
    packet pkt;
    send_packets_in_cwnd();
    while (recv_ack_index < total_pkt_num) {
        //cout << dup_ack_num << endl;
        if ((recvfrom(s, &pkt, sizeof(packet), 0, NULL, NULL)) == -1){
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                diep("recvfrom()");
            }
            if (!sent_deque.empty()) {
                detect_timeout();
//                handle_timeout();
            }
        }
        else{
            if (pkt.header.type == ACK){
                //cout << sent_deque.size() << "sent_queue size//////" << endl;
                //detect_timeout();
                //cout << "receive ack is " << pkt.header.ack_num << endl;
                handle_ack(&pkt);
            }
        }
        //detect timeout
    }
    cout<<"transmit finish"<<endl;
    terminate_sender_tcp();
    printf("Closing the socket\n");
    close(s);
    fclose(fp);
    return;

}

/*
 *
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}

