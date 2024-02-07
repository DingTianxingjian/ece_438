/*
 * File:   receiver_main.c
 * Author:
 *
 * Created on
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "utils.hpp"
#include <errno.h>
#include <iostream>
#include <vector>
#include <queue>


using namespace std;

struct sockaddr_in si_me, si_other;
int s, slen;
struct compare {
    bool operator()(packet a, packet b) {
        return  a.header.seq_num > b.header.seq_num;
    }
};
priority_queue<packet, vector<packet>, compare> no_order_pkt_buffer;

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
}

void send_ack(int ack_num, packet_type type) {
    packet p;
    p.header.ack_num = ack_num;
    p.header.type = type;
    p.header.data_len = 0;
    p.header.seq_num = 0;
    memset(p.payload, 0, MSS);
    if (sendto(s, &p, sizeof(packet), 0, (sockaddr*)&si_other, (socklen_t)sizeof (si_other))==-1){
        diep("send_ack in receiver");
    }
}


void init_receiver_tcp() {
    char buf[sizeof(packet)];
    slen = sizeof(si_other);
    while (true) {
        if (recvfrom(s, buf, sizeof(packet), 0, (struct sockaddr *)&si_other, (socklen_t*)&slen) == -1) {
            if (errno != EAGAIN || errno != EWOULDBLOCK){
                diep("recvfrom()");
            }
        }
        else {
            packet p;
            memcpy(&p, buf, sizeof(packet));
            if (p.header.type == SYN) {
                p = build_no_data_pkt(SYNACK);
                send_one_packet(&p);
            }
            else if(p.header.type == SYNACK) {
                cout << "tcp established" << endl;
                break;
            }
        }
    }
}

void terminate_receiver_tcp() {
    char buf[sizeof(packet)];
    slen = sizeof(si_other);
    while (true) {
        if (recvfrom(s, buf, sizeof(packet), 0, (struct sockaddr *)&si_other, (socklen_t*)&slen) == -1) {
            if (errno != EAGAIN || errno != EWOULDBLOCK){
                diep("recvfrom()");
            }
        }
        else {
            packet p;
            memcpy(&p, buf, sizeof(packet));
            if (p.header.type == FIN) {
                p = build_no_data_pkt(FINACK);
                send_one_packet(&p);
            }
            else if(p.header.type == FINACK) {
                cout << "tcp ended" << endl;
                break;
            }
        }
    }
}


void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {

    slen = sizeof (si_other);
    int ack_num = -1;

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");


    /* Now receive data and send acknowledgements */
    FILE* fp = fopen(destinationFile, "wb");
    if (fp == NULL) {
        diep("error in opening destination file");
    }

    //init_receiver_tcp();
    while (true) {
        packet pkt;
        if (recvfrom(s, &pkt, sizeof(packet), 0, (sockaddr*)&si_other, (socklen_t*)&slen) == -1) {
            diep("recvfrom()");
        }
        if (pkt.header.type == FIN) {
            cout << "recv FIN" << endl;
            packet p = build_no_data_pkt(FINACK);
            send_one_packet(&p);
            break;
        }
        else if (pkt.header.type == MSG) {
            //cout <<"receive" <<pkt.header.seq_num << endl;
            if (pkt.header.seq_num < ack_num + 1) {
                //cout << "receive stale packet from sender" << endl;
            }
            else if (pkt.header.seq_num == ack_num + 1) {
                //cout << "receive right packet from sender" << endl;
                fwrite(pkt.payload, sizeof (char), pkt.header.data_len, fp);
                ack_num++;
                while (!no_order_pkt_buffer.empty() && no_order_pkt_buffer.top().header.seq_num == ack_num + 1) {
                    packet p = no_order_pkt_buffer.top();
                    fwrite(p.payload, sizeof(char), p.header.data_len, fp);
                    ack_num++;
                    no_order_pkt_buffer.pop();
                }
            }
            else {
                //cout << "receive out of order packet from sender and ack is  " << ack_num <<endl;
                if (no_order_pkt_buffer.size() < MAX_RECEIVER_QUEUE_SIZE) {
                    no_order_pkt_buffer.push(pkt);
                }
                else {
                    cout << "buffer limit arrived" << endl;
                }
            }
            send_ack(ack_num, ACK);
            //cout << "send ack is" << ack_num << endl;
        }
    }

    fclose(fp);
    close(s);
    printf("%s received.", destinationFile);
    return;
}

/*
 *
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}
