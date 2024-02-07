#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>

using namespace std;

#define PORT "3490"  // the port users will be connecting to

#define MAXDATASIZE 4096 // max number of bytes we can get at once

#define BACKLOG 10	 // how many pending connections queue will hold

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    const int CHUNK_SIZE = MAXDATASIZE;
    char chunk[CHUNK_SIZE];
    char buf[MAXDATASIZE];
    FILE* file;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener

            recv(new_fd, buf, MAXDATASIZE, 0);
            string req_buf(buf);
            string resp_header;
            //handle request msg from client, send response header
            //check if request starts with GET
            if(req_buf.find("GET ") == 0) {
                size_t startPos = 4; // length of "GET "
                size_t endPos = req_buf.find(" HTTP");
                if (endPos != req_buf.npos) {
                    string path = req_buf.substr(startPos, endPos - startPos);
                    cout << "client need file in: " << path << endl;
                    file = fopen(path.data(), "rb");
                    if (file != NULL) {
                        resp_header= "HTTP/1.1 200 OK\r\n\r\n";
                    } else {
                        resp_header = "HTTP/1.1 404 Not Found\r\n\r\n";
                    }
                } else {
                    resp_header = "HTTP/1.1 400 Bad Request\r\n\r\n";
                }
            } else {
                resp_header = "HTTP/1.1 400 Bad Request\r\n\r\n";
            }
            if (send(new_fd, resp_header.data(), resp_header.size(), 0) == -1) {
                perror("send resp header error in server");
            }
            //send real file content to client
            while(true) {
                size_t numbytes = fread(chunk, 1, CHUNK_SIZE, file);
                if (numbytes == 0) {
                    if (ferror(file)) {
                        perror("fread");
                        exit(1);
                    }
                    //end of file, has already finished read process;
                    break;
                }
                if (send(new_fd, chunk, numbytes, 0) == -1) {
                    perror("send file error in server");
                    exit(1);
                }
            }
            fclose(file);  // Make sure to close the file pointer

            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}

