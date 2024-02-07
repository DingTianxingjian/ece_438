#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "UrlParser.hpp"
#include <iostream>
#include <fstream>

using namespace std;

#define PORT "3490" // the port client will be connecting to

#define MAXDATASIZE 4096 // max number of bytes we can get at once

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    string url;
    URLInfo urlInfo;
    const int CHUNK_SIZE = MAXDATASIZE;
    char chunk[CHUNK_SIZE];
    ofstream fos("output", ios::binary);
    bool isHeaderReceived = false;

    if (argc != 2) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    //resolve parameter url
    url = argv[1];
    if(URLParser::parse(url, urlInfo) != 0) {
        cerr << "Failed to parse URL: " << url << endl;
        return 1;
    } else {
        cout << "Protocol: " << urlInfo.protocol << endl;
        cout << "Host: " << urlInfo.host << endl;
        cout << "Port: " << urlInfo.port << endl;
        cout << "Path: " << urlInfo.path << endl;
    }

    if ((rv = getaddrinfo(urlInfo.host.data(), urlInfo.port.data(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
              s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    //send simple http GET request
    string get_request = "GET " + urlInfo.path + " HTTP/1.1\r\n" + "User-Agent: Wget/1.12(linux-gnu)\r\n" +
                         "Host: " + urlInfo.host + ":" + urlInfo.port + "\r\n" + "Connection: Keep-Alive\r\n\r\n";
    if(send(sockfd, get_request.c_str(), get_request.size(), 0) == -1) {
        perror("send error in client");
    }

    //receive header and file from server
    bool isHeaderProcessed = false;

    while(true) {
        numbytes = recv(sockfd, chunk, CHUNK_SIZE, 0);
        if (numbytes <= 0) break; // No data or an error.

        if (!isHeaderProcessed) {
            char* headerEnd = const_cast<char *>(strstr(chunk, "\r\n\r\n")) + 4;
            if (headerEnd) {
                isHeaderProcessed = true;

                // Print the header
                string header(chunk, headerEnd - chunk);
                printf("%s", header.c_str());

                // Write the part of the body we've received so far
                fos.write(headerEnd, numbytes - header.size());
            }
        } else {
            fos.write(chunk, numbytes);
        }
    }

    if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';

    printf("client: received\n");

    close(sockfd);

    return 0;
}


